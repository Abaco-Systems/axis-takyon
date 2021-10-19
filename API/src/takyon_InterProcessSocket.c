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
//   This is a Takyon interface to the inter-process socket interface.
//   Uses local Unix socket on linux, otherwise 127.0.0.1 on Windows.
//   No extra synchronization standards are needed since sockets have built in
//   synchronization.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define XFER_COMMAND     11223344
#define BARRIER_INIT     717171
#define BARRIER_FINALIZE 515151

typedef struct {
  uint64_t remote_max_recver_bytes; // Only used by the sender
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
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

GLOBAL_VISIBILITY bool tknSend(TakyonPath *path, int buffer_index, TakyonSendFlagsMask flags, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->send_buffer_list[buffer_index];

  // Error check flags
  if (flags != TAKYON_SEND_FLAGS_NONE) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect only supports send flags == TAKYON_SEND_FLAGS_NONE\n");
    return false;
  }

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

  // NOTE: Sockets are self signaling, so no need for an extra signaling mechanism

  return true;
}

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, TakyonRecvFlagsMask flags, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->recv_buffer_list[buffer_index];

  // Error check flags
  if (flags != TAKYON_RECV_FLAGS_NONE) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect only supports recv flags == TAKYON_RECV_FLAGS_NONE\n");
    return false;
  }

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
      printf("%-15s (%s:%s) Data was waiting: buf=%d, bytes=%ju, offset=%ju\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             buffer_index,
             buffer->recved_bytes,
             buffer->recved_offset);
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
      printf("%-15s (%s:%s) Got header: buf=%d, bytes=%ju, offset=%ju\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             recved_buffer_index,
             recved_bytes,
             recved_offset);
    }

    // Get the data
    SingleBuffer *recver_buffer = &buffers->recv_buffer_list[recved_buffer_index];
    uint64_t total_bytes = recved_bytes + recved_offset;
    uint64_t recver_max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
    if (total_bytes > recver_max_bytes) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Receiving out of bounds data. Max is %ju bytes but trying to recv %ju plus offset of %ju\n", recver_max_bytes, recved_bytes, recved_offset);
      return false;
    }
    if (recver_buffer->got_data) {
      buffers->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received data for index %d twice before the first was processed\n", recved_buffer_index);
      return false;
    }
    if (recved_bytes) {
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
        printf("%-15s (%s:%s) Got data: %ju bytes at offset %ju on buffer %d\n",
               __FUNCTION__,
               path->attrs.is_endpointA ? "A" : "B",
               path->attrs.interconnect,
               recved_bytes,
               recved_offset,
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

    // Do a round trip to enforce a barrier
    if (!buffers->connection_failed) {
      if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_FINALIZE, private_path->path_destroy_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run finalize barrier part 1\n");
        graceful_disconnect_ok = false;
      }
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

    // If the barrier completed, the close will not likely hold up any data
    socketClose(buffers->socket_fd);
  }

  // Free path memory resources
  freePathMemoryResources(path);

  return graceful_disconnect_ok;
}

GLOBAL_VISIBILITY bool tknCreate(TakyonPath *path) {
  // Supported formats:
  //   "InterProcessSocket -ID=<ID>"

  // Get interconnect params
  // Local socket
  uint32_t path_id;
  bool found;
  bool ok = argGetUInt(path->attrs.interconnect, "-ID=", &path_id, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec missing -ID=<value> for local socket\n");
    return false;
  }
  char local_socket_name[TAKYON_MAX_INTERCONNECT_CHARS];
  snprintf(local_socket_name, TAKYON_MAX_INTERCONNECT_CHARS, "TakyonSocket_%d", path_id);

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
  if (path->attrs.is_endpointA) {
    if (!socketCreateLocalClient(local_socket_name, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local client socket\n");
      goto cleanup;
    }
  } else {
    if (!socketCreateLocalServer(local_socket_name, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local server socket\n");
      goto cleanup;
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
  freePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setInterProcessSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSent = NULL;
  private_path->tknPostRecv = NULL;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
