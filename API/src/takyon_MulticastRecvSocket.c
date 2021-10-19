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
//   - Only endpoint B can be used
//   - Can only receive
//   - There can be multiple receivers connected to a single multicast sender.
//   - Can still have multiple buffers, but the sender won't have any concept
//     of multiple buffers. This is good if the receiver is organizing a bunch
//     of independentt messages via buffers
//   - takyonRecv() will always set offset_ret to zero
// -----------------------------------------------------------------------------

#include "takyon_private.h"

typedef struct {
  bool recver_addr_alloced;
  size_t recver_addr;
} SingleBuffer;

typedef struct {
  SingleBuffer *recv_buffer_list;
  TakyonSocket socket_fd;
  void *sock_in_addr;
  bool connection_failed;
} PathBuffers;

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, TakyonRecvFlagsMask flags, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Error check flags
  if (flags != TAKYON_RECV_FLAGS_NONE) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect only supports recv flags == TAKYON_RECV_FLAGS_NONE\n");
    return false;
  }

  // Verify connection is still good
  if (buffers->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The connection is no longer valid\n");
    return false;
  }

  // NOTE: Unlike TCP sockets, only one buffer is suppported, so there won't be any need to check if the buffer was previous received

  // Get the data
  SingleBuffer *buffer = &buffers->recv_buffer_list[buffer_index];
  void *recver_addr = (void *)(buffer->recver_addr);
  uint64_t max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
  uint64_t bytes_read = 0;
  if (!socketDatagramRecv(buffers->socket_fd, recver_addr, max_bytes, &bytes_read, path->attrs.is_polling, private_path->recv_start_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    buffers->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to receive data\n");
    return false;
  }
  if ((private_path->recv_start_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
  }

  // Record the results
  if (bytes_ret != NULL) *bytes_ret = bytes_read;
  if (offset_ret != NULL) *offset_ret = 0;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE) {
    printf("%-15s (%s) Got data: %ju bytes on buffer %d\n",
           __FUNCTION__,
           path->attrs.interconnect,
           bytes_read,
           buffer_index);
  }

  return true;
}

static void freePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Free buffer resources
  if (buffers->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.nbufs_AtoB;
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
    printf("%-15s (%s) destroy path\n",
           __FUNCTION__,
           path->attrs.interconnect);
  }

  // Connection was made, so disconnect gracefully
  if (path->attrs.is_polling) {
    socketSetBlocking(buffers->socket_fd, 1, path->attrs.error_message);
  }

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
  //   "MulticastRecvSocket -IP=<IP> -group=<gIP> -port=<port> [-reuse] [-rcvbuf=<bytes>]"
  //     -rcvbuf=<bytes> is used to give the kernel more buffering to help the receiver avoid dropping packets
  // Valid multicast addresses: 224.0.0.0 through 239.255.255.255, but some are reserved

  // Get interconnect params
  bool allow_reuse = argGetFlag(path->attrs.interconnect, "-reuse");
  char ip_addr[TAKYON_MAX_INTERCONNECT_CHARS];
  char multicast_group[TAKYON_MAX_INTERCONNECT_CHARS];

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

  // Kernel recv bytes; used to avoid dropping packets
  int num_kernel_buffer_bytes;
  ok = argGetInt(path->attrs.interconnect, "-rcvbuf=", &num_kernel_buffer_bytes, &found, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "optional interconnect argument incomplete: '-rcvbuf=<nbytes>'\n");
    return false;
  }
  if (!found) {
    num_kernel_buffer_bytes = 0;
  }

  // Validate the restricted attribute values
  if (path->attrs.is_endpointA) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This interconnect can only be created on endpoint B.\n");
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
  int nbufs_recver = path->attrs.nbufs_AtoB;
  buffers->recv_buffer_list = calloc(nbufs_recver, sizeof(SingleBuffer));
  if (buffers->recv_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }

  // Fill in some initial fields
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

  // Create the one sided socket
  if (!socketCreateMulticastReceiver(ip_addr, multicast_group, (uint16_t)port_number, allow_reuse, &buffers->socket_fd, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create multicast receiver socket\n");
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

  // Allow datagrams to be buffered in the OS to avoid dropping packets
  if (num_kernel_buffer_bytes > 0) {
    if (!socketSetKernelRecvBufferingSize(buffers->socket_fd, num_kernel_buffer_bytes, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to set socket's SO_RCVBUF\n");
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
void setMulticastRecvSocketFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = NULL;
  private_path->tknIsSent = NULL;
  private_path->tknPostRecv = NULL;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
