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
//   This is a Takyon interface to the inter-process and inter-processor
//   connectionless unreliable socket interface (datagram).
//   This is not a connected interconnect so the two endpoints do not
//   communicate with each other during the create or destroy phase. I.e. one
//   can live without the other.
//   Also, since this is an unreliable connection, messages could be dropped.
//   No extra synchronization standards are needed since sockets have built in
//   synchronization.
// Restrictions:
//   - Only endpoint A can be used
//   - Can only send
//   - Can still have multiple buffers, but the receiver won't have any concept
//     of multiple buffers. This is good if the sender is organizing a bunch of
//     independent messages via buffers
//   - A source offset can be used by the sender, which is helpful with organizing
//     the part of a buffer that is sent, but the receiver won't have any concept
//     of offset, so a destination offset is not allowed.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

typedef struct {
  bool sender_addr_alloced;
  size_t sender_addr;
  bool send_started;
} SingleBuffer;

typedef struct {
  SingleBuffer *send_buffer_list;
  TakyonSocket socket_fd;
  void *sock_in_addr;
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

  // Verify connection is still good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The connection is no longer valid\n");
    return false;
  }

  // NOTE: the number of bytes sent does not need to be max bytes. The receiver will detect how many were sent in the datagram.

  // Check if waiting on a takyonIsSendFinished()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
  // Transfer the data, no header to send
  if (!socketDatagramSend(buffers->socket_fd, buffers->sock_in_addr, sender_addr, bytes, path->attrs.is_polling, private_path->send_start_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer data\n");
    return false;
  }
  if ((private_path->send_start_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
  }

  // NOTE: Sockets are self signaling, so no need for an extra signaling mechanism

  // Handle completion
  buffer->send_started = true;
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

  // Verify connection is still good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The connection is no longer valid\n");
    return false;
  }

  // Check the transfer progress
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
  TAKYON_RECORD_ERROR(path->attrs.error_message, "takyonRecv() is not supported with this interconnect\n");
  return false;
}

static void freePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Free buffer resources
  if (buffers->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the send buffers, if path managed
      if (buffers->send_buffer_list[buf_index].sender_addr_alloced && (buffers->send_buffer_list[buf_index].sender_addr != 0)) {
        memoryFree((void *)buffers->send_buffer_list[buf_index].sender_addr, path->attrs.error_message);
      }
    }
    free(buffers->send_buffer_list);
  }

  // Free the private handle
  free(buffers);
}

GLOBAL_VISIBILITY bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s) destroy path\n",
           __FUNCTION__,
           path->attrs.interconnect);
  }

  // Connection was made, so disconnect gracefully
  if (path->attrs.is_polling) {
    socketSetBlocking(buffers->socket_fd, 1, path->attrs.error_message);
  }

  // Extra cleanup
  free(buffers->sock_in_addr);

  // Sleep here or else remote side might error out from a disconnect message while the remote process completing it's last transfer and waiting for any TCP ACKs to validate a complete transfer
  clockSleepYield(MICROSECONDS_TO_SLEEP_BEFORE_DISCONNECTING);

  // If the barrier completed, the close will not likely hold up any data
  socketClose(buffers->socket_fd);

  // Free path memory resources
  freePathMemoryResources(path);

  return true;
}

GLOBAL_VISIBILITY bool tknCreate(TakyonPath *path) {
  // Supported formats:
  //   "UnicastSendSocket -IP=<IP> -port=<port>"

  // Get interconnect params
  char ip_addr[TAKYON_MAX_INTERCONNECT_CHARS];

  // Destination IP address
  bool found = false;
  bool ok = argGetText(path->attrs.interconnect, "-IP=", ip_addr, TAKYON_MAX_INTERCONNECT_CHARS, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for a unicast sender socket must have -IP=<IP>\n");
    return false;
  }

  // Port number
  uint32_t port_number = 0;
  ok = argGetUInt(path->attrs.interconnect, "-port=", &port_number, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec missing -port=<value> for socket\n");
    return false;
  }
  if ((port_number < 1024) || (port_number > 65535)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "port numbers need to be between 1024 and 65535\n");
    return false;
  }

  // Validate the restricted attribute values
  if (!path->attrs.is_endpointA) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect can only be created on endpoint A.\n");
    return false;
  }
  if ((path->attrs.nbufs_AtoB < 1) && (path->attrs.nbufs_BtoA != 0)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The datagram path requires nbufs_AtoB to be >= 1 and nbufs_BtoA to be 0.\n");
    return false;
  }

  // Allocate private handle
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = calloc(1, sizeof(PathBuffers));
  if (buffers == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  buffers->socket_fd = -1;
  buffers->connection_failed = false;
  private_path->private_data = buffers;

  // Allocate the buffers list
  int nbufs_sender = path->attrs.nbufs_AtoB;
  buffers->send_buffer_list = calloc(nbufs_sender, sizeof(SingleBuffer));
  if (buffers->send_buffer_list == NULL) {
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

  // Create the one sided socket
  if (!socketCreateUnicastSender(ip_addr, (uint16_t)port_number, &buffers->socket_fd, &buffers->sock_in_addr, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create UDP sender socket\n");
    goto cleanup;
  }

  // Set to polling mode if needed
  if (path->attrs.is_polling) {
    if (!socketSetBlocking(buffers->socket_fd, 0, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Could not set the UDP socket to be non blocking (i.e. polling)\n");
      socketClose(buffers->socket_fd);
      goto cleanup;
    }
  }

  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  freePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setUnicastSendSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
