// Copyright 2018,2020 Abaco Systems
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// -----------------------------------------------------------------------------
// Description:
//   This is a Takyon interface to the inter-processor TCP socket interface.
//   No extra synchronization standards are needed since sockets have built in
//   synchronization.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define XFER_COMMAND     99887766
#define BARRIER_INIT     727272
#define BARRIER_FINALIZE 525252

typedef struct {
  uint64_t remote_max_recver_bytes; // Only used by the sender
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
  bool send_started;
  bool got_data;
  uint64_t recved_bytes;
  uint64_t recved_offset;
} SingleBuffer;

typedef struct {
  SingleBuffer *send_buffer_list;
  SingleBuffer *recv_buffer_list;
  TakyonSocket socket_fd;
  bool connection_made;
  bool connection_failed;
} PathBuffers;

GLOBAL_VISIBILITY bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->send_buffer_list[buffer_index];

  // Verify connection is good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  // Validate fits on recver
  uint64_t total_bytes_to_recv = dest_offset + bytes;
  if (total_bytes_to_recv > buffer->remote_max_recver_bytes) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of range for the recver. Exceeding by %lld bytes\n", total_bytes_to_recv - buffer->remote_max_recver_bytes);
    return false;
  }

  // Check if waiting on a takyonIsSendFinished()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
  // First transfer the header
  uint64_t header[4];
  header[0] = XFER_COMMAND;
  header[1] = buffer_index;
  header[2] = bytes;
  header[3] = dest_offset;
  if (!socketSend(buffers->socket_fd, header, sizeof(header), path->attrs.is_polling, private_path->send_start_timeout_ns, private_path->send_start_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer header\n");
    return false;
  }
  if ((private_path->send_start_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
  }

  // Send the data
  if (bytes > 0) {
    if (!socketSend(buffers->socket_fd, sender_addr, bytes, path->attrs.is_polling, private_path->send_finish_timeout_ns, private_path->send_finish_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer data\n");
      return false;
    }
    if ((private_path->send_finish_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but data was transfered
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Timed out in the middle of a transfer\n");
      return false;
    }
  }
  buffer->send_started = true;

  // NOTE: Sockets are self signaling, so no need for an extra signaling mechanism

  // Handle completion
  if (path->attrs.send_completion_method == TAKYON_BLOCKING) {
    buffer->send_started = false;
  } else if (path->attrs.send_completion_method == TAKYON_USE_IS_SEND_FINISHED) {
    // Nothing to do
  }

  return true;
}

GLOBAL_VISIBILITY bool tknIsSendFinished(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Verify connection is good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  SingleBuffer *buffer = &buffers->send_buffer_list[buffer_index];
  if (!buffer->send_started) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "takyonIsSendFinished() was called, but a prior takyonSend() was not called on buffer %d\n", buffer_index);
    return false;
  }
  // Since socket can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;
  return true;
}

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->recv_buffer_list[buffer_index];

  // Verify connection is good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  // Check if data already recved for this index
  if (buffer->got_data) {
    if (bytes_ret != NULL) *bytes_ret = buffer->recved_bytes;
    if (offset_ret != NULL) *offset_ret = buffer->recved_offset;
    buffer->got_data = false;
    // Verbosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE) {
      printf("%-15s (%s:%s) Data was waiting: buf=%d, bytes=%lld, offset=%lld\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             buffer_index,
             (unsigned long long)buffer->recved_bytes,
             (unsigned long long)buffer->recved_offset);
    }
    return true;
  }

  // Wait for the data
  while (1) {
    // Get the header
    uint64_t header[4];
    if (!socketRecv(buffers->socket_fd, header, sizeof(header), path->attrs.is_polling, private_path->recv_start_timeout_ns, private_path->recv_start_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to receive header\n");
      return false;
    }
    if ((private_path->recv_start_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but no data was transfered yet
      return true;
    }
    if (header[0] != XFER_COMMAND) { 
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "got unexpected header\n");
      return false;
    }
    int recved_buffer_index = (int)header[1];
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    if (recved_buffer_index >= nbufs_recver) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "got invalid buffer index = %d\n", recved_buffer_index);
      return false;
    }
    uint64_t recved_bytes = header[2];
    uint64_t recved_offset = header[3];

    // Verbosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE) {
      printf("%-15s (%s:%s) Got header: buf=%d, bytes=%lld, offset=%lld\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             recved_buffer_index,
             (unsigned long long)recved_bytes,
             (unsigned long long)recved_offset);
    }

    // Get the data
    SingleBuffer *recver_buffer = &buffers->recv_buffer_list[recved_buffer_index];
    uint64_t total_bytes = recved_bytes + recved_offset;
    uint64_t recver_max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
    if (total_bytes > recver_max_bytes) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Receiving out of bounds data. Max is %lld bytes but trying to recv %lld plus offset of %lld\n", (unsigned long long)recver_max_bytes, (unsigned long long)recved_bytes, (unsigned long long)recved_offset);
      return false;
    }
    if (recver_buffer->got_data) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received data for index %d twice before the first was processed\n", recved_buffer_index);
      return false;
    }
    if (recved_bytes > 0) {
      void *recver_addr = (void *)(recver_buffer->recver_addr + recved_offset);
      if (!socketRecv(buffers->socket_fd, recver_addr, recved_bytes, path->attrs.is_polling, private_path->recv_finish_timeout_ns, private_path->recv_finish_timeout_ns, timed_out_ret, path->attrs.error_message)) {
        buffers->connection_failed = true;
        TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to receive data\n");
        return false;
      }
      if ((private_path->recv_finish_timeout_ns >= 0) && (*timed_out_ret == true)) {
        // Timed out but data was transfered
        buffers->connection_failed = true;
        TAKYON_RECORD_ERROR(path->attrs.error_message, "timed out in the middle of a transfer\n");
        return false;
      }
    }

    if (recved_buffer_index == buffer_index) {
      if (bytes_ret != NULL) *bytes_ret = recved_bytes;
      if (offset_ret != NULL) *offset_ret = recved_offset;
      // Verbosity
      if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE) {
        printf("%-15s (%s:%s) Got data: %lld bytes at offset %lld on buffer %d\n",
               __FUNCTION__,
               path->attrs.is_endpointA ? "A" : "B",
               path->attrs.interconnect,
               (unsigned long long)recved_bytes,
               (unsigned long long)recved_offset,
               buffer_index);
      }
      break;
    } else {
      // Mark the unexpected buffer got data
      recver_buffer->got_data = true;
      recver_buffer->recved_bytes = recved_bytes;
      recver_buffer->recved_offset = recved_offset;
    }
  }

  return true;
}

static void freePathResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Free buffer resources
  if (buffers->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the send buffers, if path managed
      if (buffers->send_buffer_list[buf_index].sender_addr_alloced && (buffers->send_buffer_list[buf_index].sender_addr != 0)) {
        memoryFree((void *)buffers->send_buffer_list[buf_index].sender_addr, path->attrs.error_message);
      }
    }
    free(buffers->send_buffer_list);
  }
  if (buffers->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      // Free the recv buffers, if path managed
      if (buffers->recv_buffer_list[buf_index].recver_addr_alloced && (buffers->recv_buffer_list[buf_index].recver_addr != 0)) {
        memoryFree((void *)buffers->recv_buffer_list[buf_index].recver_addr, path->attrs.error_message);
      }
    }
    free(buffers->recv_buffer_list);
  }

  // Free the private handle
  free(buffers);
}

GLOBAL_VISIBILITY bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  bool graceful_disconnect_ok = true;
  if (buffers->connection_made) {
    // Connection was made, so disconnect gracefully
    if (path->attrs.is_polling) {
      socketSetBlocking(buffers->socket_fd, 1, path->attrs.error_message);
    }

    // Verbosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
      printf("%-15s (%s:%s) Disconnecting\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect);
    }

    // Do a round trip to enforce a barrier
    if (!buffers->connection_failed) {
      if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_FINALIZE, private_path->path_destroy_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run finalize barrier\n");
        graceful_disconnect_ok = false;
      }
    }

    // Sleep here or else remote side might error out from a disconnect message while the remote process completing it's last transfer and waiting for any TCP ACKs to validate a complete transfer
    clockSleepYield(MICROSECONDS_TO_SLEEP_BEFORE_DISCONNECTING);

    // If the barrier completed, the close will not likely hold up any data
    socketClose(buffers->socket_fd);
  }

  // Free path memory resources
  freePathResources(path);

  return graceful_disconnect_ok;
}

GLOBAL_VISIBILITY bool tknCreate(TakyonPath *path) {
  // Verify the number of buffers
  if (path->attrs.nbufs_AtoB <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect requires attributes->nbufs_AtoB > 0\n");
    return false;
  }
  if (path->attrs.nbufs_BtoA <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect requires attributes->nbufs_BtoA > 0\n");
    return false;
  }

  // Supported formats:
  //   User assigned port number:
  //   "Socket -client -IP=<IP> -port=<port>"
  //   "Socket -server -IP=<IP> -port=<port> [-reuse]"  // <IP> can be 'Any'
  //
  // Ephemeral port number (assigned by system)
  //   "Socket -client -IP=<IP> -ID=<id>"
  //   "Socket -server -IP=<IP> -ID=<id>"  // <IP> can be 'Any'

  char interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS];
  if (!argGetInterconnect(path->attrs.interconnect, interconnect_name, TAKYON_MAX_INTERCONNECT_CHARS, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get interconnect name\n");
    return false;
  }

  // Determine if client or server
  bool is_client = argGetFlag(path->attrs.interconnect, "-client");
  bool is_server = argGetFlag(path->attrs.interconnect, "-server");
  if (!is_client && !is_server) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Interconnect spec for this interconnect must specifiy one of -client or -server\n");
    return false;
  }
  if (is_client && is_server) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Interconnect spec for this interconnect can't specifiy both -client and -server\n");
    return false;
  }

  bool allow_reuse = argGetFlag(path->attrs.interconnect, "-reuse");

  // IP address
  char ip_addr[TAKYON_MAX_INTERCONNECT_CHARS];
  bool found = false;
  bool ok = argGetText(path->attrs.interconnect, "-IP=", ip_addr, TAKYON_MAX_INTERCONNECT_CHARS, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for socket must have -IP=<IP>. <IP> can be 'Any' if it's the server.\n");
    return false;
  }

  // Get the path ID
  uint32_t path_id = 0;
  bool path_id_found = false;
  ok = argGetUInt(path->attrs.interconnect, "-ID=", &path_id, &path_id_found, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec -ID=<id> is invalid\n");
    return false;
  }

  // Get the port number
  uint32_t temp_port_number = 0;
  bool port_number_found = false;
  ok = argGetUInt(path->attrs.interconnect, "-port=", &temp_port_number, &port_number_found, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec -port=<value> is invalid\n");
    return false;
  }
  if (port_number_found) {
    if ((temp_port_number < 1024) || (temp_port_number > 65535)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "port numbers need to be between 1024 and 65535\n");
      return false;
    }
  }
  unsigned short port_number = temp_port_number;

  // Verify only path ID or port number set
  if (path_id_found && port_number_found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Only specify one of ID=<id> or port=<value>\n");
    return false;
  }
  if (!path_id_found && !port_number_found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Need to specify one of ID=<id> or port=<value>\n");
    return false;
  }

  // Allocate private handle
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = calloc(1, sizeof(PathBuffers));
  if (buffers == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  buffers->connection_failed = false;
  buffers->connection_made = false;
  buffers->socket_fd = -1;
  private_path->private_data = buffers;

  // Allocate the buffers list
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  buffers->send_buffer_list = calloc(nbufs_sender, sizeof(SingleBuffer));
  if (buffers->send_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }
  buffers->recv_buffer_list = calloc(nbufs_recver, sizeof(SingleBuffer));
  if (buffers->recv_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }

  // Fill in some initial fields
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    uint64_t sender_bytes = path->attrs.sender_max_bytes_list[buf_index];
    // Sender
    if (sender_bytes > 0) {
      size_t sender_addr = path->attrs.sender_addr_list[buf_index];
      if (sender_addr == 0) {
        buffers->send_buffer_list[buf_index].sender_addr_alloced = true;
      } else {
        buffers->send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    // Recver
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr == 0) {
        buffers->recv_buffer_list[buf_index].recver_addr_alloced = true;
      } else {
        buffers->recv_buffer_list[buf_index].recver_addr = recver_addr;
      }
    }
  }

  // Create local sender memory
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    uint64_t sender_bytes = path->attrs.sender_max_bytes_list[buf_index];
    if ((sender_bytes > 0) && (path->attrs.sender_addr_list[buf_index] == 0)) {
      int alignment = memoryPageSize();
      void *addr;
      if (!memoryAlloc(alignment, sender_bytes, &addr, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
        goto cleanup;
      }
      path->attrs.sender_addr_list[buf_index] = (size_t)addr;
      buffers->send_buffer_list[buf_index].sender_addr = (size_t)addr;
    }
  }

  // Create local recver memory
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if ((recver_bytes > 0) && (path->attrs.recver_addr_list[buf_index] == 0)) {
      int alignment = memoryPageSize();
      void *addr;
      if (!memoryAlloc(alignment, recver_bytes, &addr, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
        goto cleanup;
      }
      path->attrs.recver_addr_list[buf_index] = (size_t)addr;
      buffers->recv_buffer_list[buf_index].recver_addr = (size_t)addr;
    }
  }

  // Create the socket and connect with remote endpoint
  if (is_client) {
    if (path_id_found) {
      if (!socketCreateEphemeralTcpClient(ip_addr, interconnect_name, path_id, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.verbosity, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP client socket\n");
        goto cleanup;
      }
    } else {
      if (!socketCreateTcpClient(ip_addr, port_number, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP client socket\n");
        goto cleanup;
      }
    }
  } else {
    if (path_id_found) {
      if (!socketCreateEphemeralTcpServer(ip_addr, interconnect_name, path_id, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.verbosity, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP server socket\n");
        goto cleanup;
      }
    } else {
      if (!socketCreateTcpServer(ip_addr, port_number, allow_reuse, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP server socket\n");
        goto cleanup;
      }
    }
  }

  // Socket round trip, to make sure the socket is really connected
  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s:%s) Init barrier\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }
  if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_INIT, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier\n");
    socketClose(buffers->socket_fd);
    goto cleanup;
  }

  // Verify endpoints have the same attributes
  if (!socketSwapAndVerifyInt(buffers->socket_fd, path->attrs.nbufs_AtoB, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_AtoB: (this=%d)\n", path->attrs.nbufs_AtoB);
    socketClose(buffers->socket_fd);
    goto cleanup;
  }
  if (!socketSwapAndVerifyInt(buffers->socket_fd, path->attrs.nbufs_BtoA, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_BtoA: (this=%d)\n", path->attrs.nbufs_BtoA);
    socketClose(buffers->socket_fd);
    goto cleanup;
  }

  // Make sure each sender knows the remote recver buffer sizes
  if (path->attrs.is_endpointA) {
    // Step 1: Endpoint A: send recver sizes to B
    for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
      if (!socketSendUInt64(buffers->socket_fd, path->attrs.recver_max_bytes_list[i], private_path->path_create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
    // Step 4: Endpoint A: recv recver sizes from B
    for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
      if (!socketRecvUInt64(buffers->socket_fd, &buffers->send_buffer_list[i].remote_max_recver_bytes, private_path->path_create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
  } else {
    // Step 2: Endpoint B: recv recver sizes from A
    for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
      if (!socketRecvUInt64(buffers->socket_fd, &buffers->send_buffer_list[i].remote_max_recver_bytes, private_path->path_create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
    // Step 3: Endpoint B: send recver sizes to A
    for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
      if (!socketSendUInt64(buffers->socket_fd, path->attrs.recver_max_bytes_list[i], private_path->path_create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
  }

  // Set to polling mode if needed
  if (path->attrs.is_polling) {
    if (!socketSetBlocking(buffers->socket_fd, 0, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Could not set the socket to be non blocking (i.e. polling)\n");
      socketClose(buffers->socket_fd);
      goto cleanup;
    }
  }

  buffers->connection_made = true;
  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  freePathResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
