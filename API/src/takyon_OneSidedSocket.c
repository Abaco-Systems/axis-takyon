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
//   This is a Takyon interface to a one sided TCP socket interface.
//   This is useful when connecting to a socket outside of the Takyon
//   world.
//   No extra synchronization standards are needed since sockets have built in
//   synchronization.
// Restrictions:
//   - Only endpoint A can be used
//   - There can be multiple send/recv buffers, but the server side will only
//     interpet data as a contiguous stream, and NOT as multiple independent
//     buffers. The server won't be able to send/recv headers with a byte count
//     or offset.
//   - 'bytes' and 'offset' must not be NULL and set before calling takyonRecv().
//     This is needed since the remote side does not send a header before the
//     data.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

typedef struct {
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
  bool send_started;
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

  // Verify dest_offset is zero
  if (dest_offset != 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect does not support non-zero destination offsets\n");
    return false;
  }

  // Verify something is being sent
  if (bytes == 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect does not support zero-byte transfer\n");
    return false;
  }

  // Verify connection is good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  // NOTE: There's limit to the data being sent to the receiver since it's just seen as a contiguous stream

  // Check if waiting on a takyonIsSendFinished()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);

  // NOTE: Can't send a header with buffer index, bytes, and offset since this is not a two-way takyon path

  // Send the data
  if (!socketSend(buffers->socket_fd, sender_addr, bytes, path->attrs.is_polling, private_path->send_start_timeout_ns, private_path->send_finish_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer data\n");
    return false;
  }
  if ((private_path->send_start_timeout_ns >= 0 || private_path->send_finish_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
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

  // Verify bytes and offset are set
  if (bytes_ret == NULL) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The values of 'bytes' can not be NULL, and must be set before the call to takyonRecv(). This is just a special case with this interconnect since the remote side does not send a header with that information.\n");
    return false;
  }
  if (offset_ret == NULL) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The values of 'offset' can not be NULL, and must be set before the call to takyonRecv(). This is just a special case with this interconnect since the remote side does not send a header with that information.\n");
    return false;
  }

  // Verify the buffer size
  uint64_t bytes_to_read = *bytes_ret;
  uint64_t total_bytes = bytes_to_read + *offset_ret;
  uint64_t max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
  if (total_bytes > max_bytes) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Receiving out of bounds data. *bytes = %ju and *offset = %ju, but the buffer size is %ju. Make sure the values of 'bytes' and 'offset' are set before making the call to takyonRecv(), this is just a special case with this interconnect since the remote side does not send a header with that information.\n", *bytes_ret, *offset_ret, max_bytes);
    return false;
  }

  // Get the data
  void *recver_addr = (void *)(buffer->recver_addr + *offset_ret);
  if (!socketRecv(buffers->socket_fd, recver_addr, bytes_to_read, path->attrs.is_polling, private_path->recv_start_timeout_ns, private_path->recv_finish_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to receive data\n");
    return false;
  }
  if ((private_path->recv_start_timeout_ns >= 0 || private_path->recv_finish_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
  }

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE) {
    printf("%-15s (%s:%s) Got data: %ju bytes on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           max_bytes,
           buffer_index);
  }

  return true;
}

static void freePathMemoryResources(TakyonPath *path) {
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

    // Sleep here or else remote side might error out from a disconnect message while the remote process completing it's last transfer and waiting for any TCP ACKs to validate a complete transfer
    clockSleepYield(MICROSECONDS_TO_SLEEP_BEFORE_DISCONNECTING);

    // Closing should flush any data still on the socket
    socketClose(buffers->socket_fd);
  }

  // Free path memory resources
  freePathMemoryResources(path);

  return graceful_disconnect_ok;
}

GLOBAL_VISIBILITY bool tknCreate(TakyonPath *path) {
  // Verify the number of buffers
  if (path->attrs.nbufs_AtoB <= 0 && path->attrs.nbufs_BtoA <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect requires one or both of attributes->nbufs_AtoB and attributes->nbufs_BtoA to be greater than zero\n");
    return false;
  }

  // Supported formats:
  //   "OneSidedSocket -client -IP=<IP> -port=<port>"
  //   "OneSidedSocket -server -IP=<IP> -port=<port> [-reuse]"  // <IP> can be 'Any'

  // Determine if client or server
  bool is_client = argGetFlag(path->attrs.interconnect, "-client");
  bool is_server = argGetFlag(path->attrs.interconnect, "-server");
  if (!is_client && !is_server) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Interconnect spec for this interconnect must specify one of -client or -server\n");
    return false;
  }
  if (is_client && is_server) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Interconnect spec for this interconnect can't specify both -client and -server\n");
    return false;
  }
  if (is_client) {
    // Verify this is endpoint A
    if (!path->attrs.is_endpointA) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect can only be created on endpoint A when -client is specified.\n");
      return false;
    }
  }
  if (is_server) {
    // Verify this is endpoint B
    if (path->attrs.is_endpointA) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect can only be created on endpoint B when -server is specified.\n");
      return false;
    }
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

  // Get the port number
  uint32_t port_number = 0;
  ok = argGetUInt(path->attrs.interconnect, "-port=", &port_number, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec missing -port=<value> for TCP socket\n");
    return false;
  }
  if ((port_number < 1024) || (port_number > 65535)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "port numbers need to be between 1024 and 65535\n");
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
  if (nbufs_sender > 0) {
    buffers->send_buffer_list = calloc(nbufs_sender, sizeof(SingleBuffer));
    if (buffers->send_buffer_list == NULL) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
      goto cleanup;
    }
  }
  if (nbufs_recver > 0) {
    buffers->recv_buffer_list = calloc(nbufs_recver, sizeof(SingleBuffer));
    if (buffers->recv_buffer_list == NULL) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
      goto cleanup;
    }
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
    if (!socketCreateTcpClient(ip_addr, (uint16_t)port_number, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP client socket\n");
      goto cleanup;
    }
  } else {
    if (!socketCreateTcpServer(ip_addr, (uint16_t)port_number, allow_reuse, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP server socket\n");
      goto cleanup;
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
  freePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setOneSidedSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
