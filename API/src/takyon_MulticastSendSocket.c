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
//   connectionless unreliable socket interface (datagram and multicast).
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
//     independentt messages via buffers
//   - An offset can be used by the sender, which is helpful with organizing the
//     part of a buffer that is sent, but the receivers won't have any concept
//     of offset.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

typedef struct {
  bool sender_addr_alloced;
  size_t sender_addr;
} SingleBuffer;

typedef struct {
  SingleBuffer *send_buffer_list;
  TakyonSocket socket_fd;
  void *sock_in_addr;
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

  // Transfer the data, no header to send
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
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

  return true;
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
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
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
  //   "MulticastSendSocket -IP=<IP> -group=<gIP> -port=<port> [-noLoopback] [-TTL=<n>]"
  // Valid multicast addresses: 224.0.0.0 through 239.255.255.255, but some are reserved
  // Supported TTL values:
  //   0:   Are restricted to the same host
  //   1:   Are restricted to the same subnet
  //   32:  Are restricted to the same site
  //   64:  Are restricted to the same region
  //   128: Are restricted to the same continent
  //   255: Are unrestricted in scope

  // Get interconnect params
  bool disable_loopback = false;
  char ip_addr[TAKYON_MAX_INTERCONNECT_CHARS];
  char multicast_group[TAKYON_MAX_INTERCONNECT_CHARS];
  uint32_t ttl_level = 1;

  // local IP address
  bool found = false;
  bool ok = argGetText(path->attrs.interconnect, "-IP=", ip_addr, TAKYON_MAX_INTERCONNECT_CHARS, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for multicast socket must have -IP=<IP>\n");
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

  // Multicast group
  ok = argGetText(path->attrs.interconnect, "-group=", multicast_group, TAKYON_MAX_INTERCONNECT_CHARS, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for multicast must have -group=<IP>, where <IP> is the multicast group address.\n");
    return false;
  }

  // Sender params
  disable_loopback = argGetFlag(path->attrs.interconnect, "-noLoopback");
  ok = argGetUInt(path->attrs.interconnect, "-TTL=", &ttl_level, &found, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for multicast must have -TTL=<n>, where <n> is the time-to-live level. The default is 1 (same subnet).\n");
    return false;
  }
  if (!found) {
    // Set the default to something safe
    ttl_level = 1;
  } else if (ttl_level > 255) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "-TTL values need to be between 0 and 255\n");
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
  if (!socketCreateMulticastSender(ip_addr, multicast_group, (uint16_t)port_number, disable_loopback, ttl_level, &buffers->socket_fd, &buffers->sock_in_addr, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create multicast sender socket\n");
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
void setMulticastSendSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSent = NULL;
  private_path->tknPostRecv = NULL;
  private_path->tknRecv = NULL;
  private_path->tknDestroy = tknDestroy;
}
#endif
