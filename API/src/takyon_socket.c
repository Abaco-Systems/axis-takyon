// Copyright 2018 Abaco Systems
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
//   This is a Takyon interface to the inter-process and inter-processor socket
//   interface.
//   This supports two flavors of sockets:
//      - local Unix socket on linux, otherwise 127.0.0.1 can be used on Windows.
//      - TCP sockets, which can be used across processor on the same network.
//   No extra synchronization standards are needed since sockets have built in
//   synchronization.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define XFER_COMMAND     123456789
#define BARRIER_INIT     777777
#define BARRIER_FINALIZE 555555

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
} XferBuffer;

typedef struct {
  XferBuffer *send_buffer_list;
  XferBuffer *recv_buffer_list;
  TakyonSocket socket_fd;
  bool connection_made;
  bool connection_failed;
} SocketPath;

#ifdef _WIN32
static
#endif
bool tknSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;
  socket_path->connection_failed = true;
  TAKYON_RECORD_ERROR(path->attrs.error_message, "Sockets do not support strided sends.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;
  XferBuffer *buffer = &socket_path->send_buffer_list[buffer_index];

  // Verify connection is good
  if (socket_path->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  // Validate fits on recver
  uint64_t total_bytes_to_recv = dest_offset + bytes;
  if (total_bytes_to_recv > buffer->remote_max_recver_bytes) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of range for the recver. Exceeding by %lld bytes\n", total_bytes_to_recv - buffer->remote_max_recver_bytes);
    return false;
  }

  // Check if waiting on a sendTest()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonSendTest() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
  // First trasfer the header
  uint64_t header[4];
  header[0] = XFER_COMMAND;
  header[1] = buffer_index;
  header[2] = bytes;
  header[3] = dest_offset;
  if (!socketSend(socket_path->socket_fd, header, sizeof(header), path->attrs.is_polling, private_path->send_complete_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    socket_path->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer header\n");
    return false;
  }
  if ((private_path->send_complete_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
  }

  // Send the data
  if (!socketSend(socket_path->socket_fd, sender_addr, bytes, path->attrs.is_polling, private_path->send_complete_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    socket_path->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer data\n");
    return false;
  }
  if ((private_path->send_complete_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but data was transfered
    socket_path->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Timed out in the middle of a transfer\n");
    return false;
  }
  buffer->send_started = true;

  // NOTE: Sockets are self signaling, so no need for an extra signaling mechanism

  // Handle completion
  if (path->attrs.send_completion_method == TAKYON_BLOCKING) {
    buffer->send_started = false;
  } else if (path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    // Nothing to do
  }

  return true;
}

#ifdef _WIN32
static
#endif
bool tknSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;

  // Verify connection is good
  if (socket_path->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  XferBuffer *buffer = &socket_path->send_buffer_list[buffer_index];
  if (!buffer->send_started) {
    socket_path->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "sendTest() was called, but a prior send() was not called on buffer %d\n", buffer_index);
    return false;
  }
  // Since socket can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;
  return true;
}

#ifdef _WIN32
static
#endif
bool tknRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;
  socket_path->connection_failed = true;
  TAKYON_RECORD_ERROR(path->attrs.error_message, "Sockets do not support strided receives.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;
  XferBuffer *buffer = &socket_path->recv_buffer_list[buffer_index];

  // Verify connection is good
  if (socket_path->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Connection is broken\n");
    return false;
  }

  // Check if data already recved for this index
  if (buffer->got_data) {
    if (bytes_ret != NULL) *bytes_ret = buffer->recved_bytes;
    if (offset_ret != NULL) *offset_ret = buffer->recved_offset;
    buffer->got_data = false;
    // Verbosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
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
    if (!socketRecv(socket_path->socket_fd, header, sizeof(header), path->attrs.is_polling, private_path->recv_complete_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to wait for header\n");
      return false;
    }
    if ((private_path->recv_complete_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but no data was transfered yet
      return true;
    }
    if (header[0] != XFER_COMMAND) { 
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "got unexpected header\n");
      return false;
    }
    int recved_buffer_index = (int)header[1];
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    if (recved_buffer_index >= nbufs_recver) {
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "got invalid buffer index = %d\n", recved_buffer_index);
      return false;
    }
    uint64_t recved_bytes = header[2];
    uint64_t recved_offset = header[3];

    // Verbosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
      printf("%-15s (%s:%s) Got header: buf=%d, bytes=%lld, offset=%lld\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             recved_buffer_index,
             (unsigned long long)recved_bytes,
             (unsigned long long)recved_offset);
    }

    // Get the data
    XferBuffer *recver_buffer = &socket_path->recv_buffer_list[recved_buffer_index];
    uint64_t total_bytes = recved_bytes + recved_offset;
    uint64_t recver_max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
    if (total_bytes > recver_max_bytes) {
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Receiving out of bounds data. Max is %lld bytes but trying to recv %lld plus offset of %lld\n", (unsigned long long)recver_max_bytes, (unsigned long long)recved_bytes, (unsigned long long)recved_offset);
      return false;
    }
    if (recver_buffer->got_data) {
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received data for index %d twice before the first was processed\n", recved_buffer_index);
      return false;
    }
    void *recver_addr = (void *)(recver_buffer->recver_addr + recved_offset);
    if (!socketRecv(socket_path->socket_fd, recver_addr, recved_bytes, path->attrs.is_polling, private_path->recv_complete_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to wait for data\n");
      return false;
    }
    if ((private_path->recv_complete_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but data was transfered
      socket_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "timed out in the middle of a transfer\n");
      return false;
    }

    if (recved_buffer_index == buffer_index) {
      if (bytes_ret != NULL) *bytes_ret = recved_bytes;
      if (offset_ret != NULL) *offset_ret = recved_offset;
      // Verbosity
      if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
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
    }
  }

  return true;
}

static void freePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;

  // Free buffer resources
  if (socket_path->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the send buffers, if path managed
      if (socket_path->send_buffer_list[buf_index].sender_addr_alloced && (socket_path->send_buffer_list[buf_index].sender_addr != 0)) {
        memoryFree((void *)socket_path->send_buffer_list[buf_index].sender_addr, path->attrs.error_message);
      }
    }
    free(socket_path->send_buffer_list);
  }
  if (socket_path->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      // Free the recv buffers, if path managed
      if (socket_path->recv_buffer_list[buf_index].recver_addr_alloced && (socket_path->recv_buffer_list[buf_index].recver_addr != 0)) {
        memoryFree((void *)socket_path->recv_buffer_list[buf_index].recver_addr, path->attrs.error_message);
      }
    }
    free(socket_path->recv_buffer_list);
  }

  // Free the private handle
  free(socket_path);
}

#ifdef _WIN32
static
#endif
bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = (SocketPath *)private_path->private;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  bool graceful_disconnect_ok = true;
  if (socket_path->connection_made) {
    // Connection was made, so disconnect gracefully
    if (path->attrs.is_polling) {
      socketSetBlocking(socket_path->socket_fd, 1, path->attrs.error_message);
    }

    // Do a round trip to enforce a barrier
    if (!socket_path->connection_failed) {
      if (!socketBarrier(path->attrs.is_endpointA, socket_path->socket_fd, BARRIER_FINALIZE, private_path->destroy_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run finalize barrier part 1\n");
        graceful_disconnect_ok = false;
      }
    }

    // Verbosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
      printf("%-15s (%s:%s) Disconnecting\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect);
    }

    // If the barrier completed, the close will not likely hold up any data
    socketClose(socket_path->socket_fd);
  }

  // Free path memory resources
  freePathMemoryResources(path);

  return graceful_disconnect_ok;
}

#ifdef _WIN32
static
#endif
bool tknCreate(TakyonPath *path) {
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
  //   "Socket -local -ID <ID> [-reuse]"
  //   "Socket -client <IP> -port <port>"
  //   "Socket -server <IP> -port <port> [-reuse]"
  //   "Socket -server Any -port <port> [-reuse]"
  bool is_a_local_socket = argGetFlag(path->attrs.interconnect, "-local");
  int path_id;
  int temp_port_number = 0;
  bool is_client = true;
  unsigned short port_number = 0;
  char ip_addr[MAX_TAKYON_INTERCONNECT_CHARS];
  char local_socket_name[MAX_TAKYON_INTERCONNECT_CHARS];
  bool allow_reuse = argGetFlag(path->attrs.interconnect, "-reuse");

  // Get interconnect params
  if (is_a_local_socket) {
    // Local socket
    bool found;
    bool ok = argGetInt(path->attrs.interconnect, "-ID", &path_id, &found, path->attrs.error_message);
    if (!ok || !found) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -ID <value> for local socket\n");
      return false;
    }
    sprintf(local_socket_name, "TakyonSocket_%d", path_id);

  } else {
    // TCP socket
    bool found;
    bool ok = argGetText(path->attrs.interconnect, "-client", ip_addr, MAX_TAKYON_INTERCONNECT_CHARS, &found, path->attrs.error_message);
    if (!ok) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for TCP socket must have one of -server <IP> or -client <IP | Any>\n");
      return false;
    }
    if (found) {
      is_client = true;
    } else {
      ok = argGetText(path->attrs.interconnect, "-server", ip_addr, MAX_TAKYON_INTERCONNECT_CHARS, &found, path->attrs.error_message);
      if (!ok || !found) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for TCP socket must have one of -server <IP> or -client <IP | Any>\n");
        return false;
      }
      is_client = false;
    }
    ok = argGetInt(path->attrs.interconnect, "-port", &temp_port_number, &found, path->attrs.error_message);
    if (!ok || !found) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -port <value> for TCP socket\n");
      return false;
    }
    if ((temp_port_number < 1024) || (temp_port_number > 65535)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "port numbers need to be between 1024 and 65535\n");
      return false;
    }
    port_number = temp_port_number;
  }

  // Allocate private handle
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  SocketPath *socket_path = calloc(1, sizeof(SocketPath));
  if (socket_path == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  socket_path->connection_failed = false;
  socket_path->connection_made = false;
  socket_path->socket_fd = -1;
  private_path->private = socket_path;

  // Allocate the buffers list
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  socket_path->send_buffer_list = calloc(nbufs_sender, sizeof(XferBuffer));
  if (socket_path->send_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }
  socket_path->recv_buffer_list = calloc(nbufs_recver, sizeof(XferBuffer));
  if (socket_path->recv_buffer_list == NULL) {
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
        socket_path->send_buffer_list[buf_index].sender_addr_alloced = true;
      } else {
        socket_path->send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    // Recver
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr == 0) {
        socket_path->recv_buffer_list[buf_index].recver_addr_alloced = true;
      } else {
        socket_path->recv_buffer_list[buf_index].recver_addr = recver_addr;
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
      socket_path->send_buffer_list[buf_index].sender_addr = (size_t)addr;
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
      socket_path->recv_buffer_list[buf_index].recver_addr = (size_t)addr;
    }
  }

  // Create the socket and connect with remote endpoint
  if (is_a_local_socket) {
    if (path->attrs.is_endpointA) {
      if (!socketCreateLocalClient(local_socket_name, &socket_path->socket_fd, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local client socket\n");
        goto cleanup;
      }
    } else {
      if (!socketCreateLocalServer(local_socket_name, allow_reuse, &socket_path->socket_fd, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local server socket\n");
        goto cleanup;
      }
    }
  } else {
    if (is_client) {
      if (!socketCreateTcpClient(ip_addr, port_number, &socket_path->socket_fd, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP client socket\n");
        goto cleanup;
      }
    } else {
      if (!socketCreateTcpServer(ip_addr, port_number, allow_reuse, &socket_path->socket_fd, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create TCP server socket\n");
        goto cleanup;
      }
    }
  }

  // Socket round trip, to make sure the socket is really connected
  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) Init barrier\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }
  if (!socketBarrier(path->attrs.is_endpointA, socket_path->socket_fd, BARRIER_INIT, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier\n");
    socketClose(socket_path->socket_fd);
    goto cleanup;
  }

  // Verify endpoints have the same attributes
  if (!socketSwapAndVerifyInt(socket_path->socket_fd, path->attrs.nbufs_AtoB, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_AtoB: (this=%d)\n", path->attrs.nbufs_AtoB);
    socketClose(socket_path->socket_fd);
    goto cleanup;
  }
  if (!socketSwapAndVerifyInt(socket_path->socket_fd, path->attrs.nbufs_BtoA, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_BtoA: (this=%d)\n", path->attrs.nbufs_BtoA);
    socketClose(socket_path->socket_fd);
    goto cleanup;
  }

  // Make sure each sender knows the remote recver buffer sizes
  if (path->attrs.is_endpointA) {
    // Step 1: Endpoint A: send recver sizes to B
    for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
      if (!socketSendUInt64(socket_path->socket_fd, path->attrs.recver_max_bytes_list[i], private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
    // Step 4: Endpoint A: recv recver sizes from B
    for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
      if (!socketRecvUInt64(socket_path->socket_fd, &socket_path->send_buffer_list[i].remote_max_recver_bytes, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
  } else {
    // Step 2: Endpoint B: recv recver sizes from A
    for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
      if (!socketRecvUInt64(socket_path->socket_fd, &socket_path->send_buffer_list[i].remote_max_recver_bytes, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
    // Step 3: Endpoint B: send recver sizes to A
    for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
      if (!socketSendUInt64(socket_path->socket_fd, path->attrs.recver_max_bytes_list[i], private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver buffer[%d] size\n", i);
        goto cleanup;
      }
    }
  }

  // Set to polling mode if needed
  if (path->attrs.is_polling) {
    if (!socketSetBlocking(socket_path->socket_fd, 0, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Could not set the socket to be non blocking (i.e. polling)\n");
      socketClose(socket_path->socket_fd);
      goto cleanup;
    }
  }

  socket_path->connection_made = true;
  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  freePathMemoryResources(path);
  return false;
}

#ifdef _WIN32
void setSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknSendStrided = tknSendStrided;
  private_path->tknSendTest = tknSendTest;
  private_path->tknRecv = tknRecv;
  private_path->tknRecvStrided = tknRecvStrided;
  private_path->tknDestroy = tknDestroy;
}
#endif
