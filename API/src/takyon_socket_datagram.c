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
//   This is a Takyon interface to the inter-process and inter-processor
//   connectionless unreliable socket interface (datagram and multicast).
//   This is not a connected interconnect so the two endpoints do not
//   communicate with each other during the create or destroy phase.
//   Also, since this is an unreliable connection, messages could be dropped.
//   If using multicast, there can be multiple receivers.
//   This implementation will enforce endpoint A as the sender and endpoint B
//   as the receiver.
//   This is not a bi-directional interconnect.
//   No extra synchronization standards are needed since sockets have built in
//   synchronization.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

typedef struct {
  uint64_t remote_max_recver_bytes; // Only used by the sender
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
  bool send_started;
} XferBuffer;

typedef struct {
  XferBuffer *send_buffer_list;
  XferBuffer *recv_buffer_list;
  TakyonSocket socket_fd;
  void *sock_in_addr;
  bool is_unicast_sender;
  bool is_unicast_recver;
  bool is_multicast_sender;
  bool is_multicast_recver;
  bool connection_failed;
} DatagramPath;

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;
  datagram_path->connection_failed = true;
  TAKYON_RECORD_ERROR(path->attrs.error_message, "Sockets do not support strided sends.\n");
  return false;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;
  XferBuffer *buffer = &datagram_path->send_buffer_list[buffer_index];

  // Verify connection is still good
  if (datagram_path->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The connection is no longer valid\n");
    return false;
  }

  // NOTE: the number of bytes sent does not need to be max bytes. The receiver will detect how many were sent in the datagram.

  // Verify this is a sender
  if (!datagram_path->is_unicast_sender && !datagram_path->is_multicast_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This datagram endpoint is set up as a receiver. takyonSend() can not be used with this endpoint\n");
    return false;
  }

  // Check if waiting on a sendTest()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonSendTest() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
  // Transfer the data, no header to send
  if (datagram_path->is_unicast_sender || datagram_path->is_multicast_sender) {
    if (!socketDatagramSend(datagram_path->socket_fd, datagram_path->sock_in_addr, sender_addr, bytes, path->attrs.is_polling, private_path->send_complete_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      datagram_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer data\n");
      return false;
    }
    if ((private_path->send_complete_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but no data was transfered yet
      return true;
    }
  }

  // NOTE: Sockets are self signaling, so no need for an extra signaling mechanism

  // Handle completion
  buffer->send_started = true;
  if (path->attrs.send_completion_method == TAKYON_BLOCKING) {
    buffer->send_started = false;
  } else if (path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    // Nothing to do
  }

  return true;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;
  // Verify this is a sender
  if (!datagram_path->is_unicast_sender && !datagram_path->is_multicast_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This datagram endpoint is set up as a receiver. takyonSend() can not be used with this endpoint\n");
    return false;
  }
  // Verify connection is still good
  if (datagram_path->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The connection is no longer valid\n");
    return false;
  }
  // Check the transfer progress
  XferBuffer *buffer = &datagram_path->send_buffer_list[buffer_index];
  if (!buffer->send_started) {
    datagram_path->connection_failed = true;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "sendTest() was called, but a prior send() was not called on buffer %d\n", buffer_index);
    return false;
  }
  // Since socket can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;
  return true;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;
  datagram_path->connection_failed = true;
  TAKYON_RECORD_ERROR(path->attrs.error_message, "Datagram sockets do not support strided receives.\n");
  return false;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;

  // Verify connection is still good
  if (datagram_path->connection_failed) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The connection is no longer valid\n");
    return false;
  }

  // NOTE: Unlike TCP sockets, only one buffer is suppported, so there won't be any need to check if the buffer was previous received

  // Get the data
  XferBuffer *buffer = &datagram_path->recv_buffer_list[buffer_index];
  void *recver_addr = (void *)(buffer->recver_addr);
  uint64_t max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
  uint64_t bytes_read = 0;
  if (datagram_path->is_unicast_recver || datagram_path->is_multicast_recver) {
    if (!socketDatagramRecv(datagram_path->socket_fd, recver_addr, max_bytes, &bytes_read, path->attrs.is_polling, private_path->recv_complete_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      datagram_path->connection_failed = true;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to wait for data\n");
      return false;
    }
    if ((private_path->recv_complete_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but no data was transfered yet
      return true;
    }
  }

  // Record the results
  if (bytes_ret != NULL) *bytes_ret = bytes_read;
  if (offset_ret != NULL) *offset_ret = 0;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
    printf("%-15s (%s:%s) Got data: %lld bytes on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           (unsigned long long)bytes_read,
           buffer_index);
  }

  return true;
}

static void freePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;

  // Free buffer resources
  if (datagram_path->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the send buffers, if path managed
      if (datagram_path->send_buffer_list[buf_index].sender_addr_alloced && (datagram_path->send_buffer_list[buf_index].sender_addr != 0)) {
        memoryFree((void *)datagram_path->send_buffer_list[buf_index].sender_addr, path->attrs.error_message);
      }
    }
    free(datagram_path->send_buffer_list);
  }
  if (datagram_path->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      // Free the recv buffers, if path managed
      if (datagram_path->recv_buffer_list[buf_index].recver_addr_alloced && (datagram_path->recv_buffer_list[buf_index].recver_addr != 0)) {
        memoryFree((void *)datagram_path->recv_buffer_list[buf_index].recver_addr, path->attrs.error_message);
      }
    }
    free(datagram_path->recv_buffer_list);
  }

  // Free the private handle
  free(datagram_path);
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  DatagramPath *datagram_path = (DatagramPath *)private_path->private;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  // Connection was made, so disconnect gracefully
  if (path->attrs.is_polling) {
    socketSetBlocking(datagram_path->socket_fd, 1, path->attrs.error_message);
  }

  // Extra cleanup
  if (datagram_path->is_unicast_sender || datagram_path->is_multicast_sender) {
    free(datagram_path->sock_in_addr);
  }

  // If the barrier completed, the close will not likely hold up any data
  socketClose(datagram_path->socket_fd);

  // Free path memory resources
  freePathMemoryResources(path);

  return true;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknCreate(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  // Supported formats:
  //   "SocketDatagram -unicastSend -client <IP> -port <port>"
  //   "SocketDatagram -unicastRecv -server <IP> -port <port> [-reuse]"
  //   "SocketDatagram -unicastRecv -server Any -port <port> [-reuse]"
  //   "SocketDatagram -multicastSend -server <IP> -group <IP> -port <port> [-disable_loopback] [-TTL <n>]"
  //   "SocketDatagram -multicastRecv -server <IP> -group <IP> -port <port> [-reuse]"
  // Valid multicast addresses: 224.0.0.0 through 239.255.255.255, but some are reserved
  // Supported TTL values:
  //   0:   Are restricted to the same host
  //   1:   Are restricted to the same subnet
  //   32:  Are restricted to the same site
  //   64:  Are restricted to the same region
  //   128: Are restricted to the same continent
  //   255: Are unrestricted in scope
  bool is_unicast_sender = argGetFlag(path->attrs.interconnect, "-unicastSend");
  bool is_unicast_recver = argGetFlag(path->attrs.interconnect, "-unicastRecv");
  bool is_multicast_sender = argGetFlag(path->attrs.interconnect, "-multicastSend");
  bool is_multicast_recver = argGetFlag(path->attrs.interconnect, "-multicastRecv");
  bool is_sender = is_unicast_sender || is_multicast_sender;
  bool is_recver = is_unicast_recver || is_multicast_recver;
  bool is_udp = is_unicast_sender || is_unicast_recver;
  bool is_multicast = is_multicast_sender || is_multicast_recver;
  if ((is_sender && is_recver) || (is_udp && is_multicast) || (!is_sender && !is_recver) || (!is_udp && !is_multicast)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The socket datagram interconnect requires one of the following flags: -unicastSend, -unicastRecv, -multicastSend, -multicastRecv\n");
    return false;
  }

  // Get interconnect params
  bool allow_reuse = argGetFlag(path->attrs.interconnect, "-reuse");
  bool disable_loopback = false;
  unsigned short port_number = 0;
  char ip_addr[MAX_TAKYON_INTERCONNECT_CHARS];
  char multicast_group[MAX_TAKYON_INTERCONNECT_CHARS];
  int ttl_level = 1;

  // local IP address
  bool found = false;
  bool ok;
  if (is_unicast_sender) {
    ok = argGetText(path->attrs.interconnect, "-client", ip_addr, MAX_TAKYON_INTERCONNECT_CHARS, &found, path->attrs.error_message);
    if (!ok || !found) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for a unicast sender socket must have -client <IP>\n");
      return false;
    }
  } else if (is_unicast_recver) {
    ok = argGetText(path->attrs.interconnect, "-server", ip_addr, MAX_TAKYON_INTERCONNECT_CHARS, &found, path->attrs.error_message);
    if (!ok || !found) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for a unicast receiver socket must have -server <IP>, where <IP> can be 'Any'\n");
      return false;
    }
  } else {
    ok = argGetText(path->attrs.interconnect, "-server", ip_addr, MAX_TAKYON_INTERCONNECT_CHARS, &found, path->attrs.error_message);
    if (!ok || !found) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for datagram multicast socket must have -server <IP>\n");
      return false;
    }
  }

  // Port number
  int temp_port_number = 0;
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

  // Multicast params
  if (is_multicast) {
    // Multicast group
    ok = argGetText(path->attrs.interconnect, "-group", multicast_group, MAX_TAKYON_INTERCONNECT_CHARS, &found, path->attrs.error_message);
    if (!ok || !found) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for multicast must have -group <IP>, where <IP> is the multicast group address.\n");
      return false;
    }

    // Sender params
    if (is_multicast_sender) {
      disable_loopback = argGetFlag(path->attrs.interconnect, "-disable_loopback");
      ok = argGetInt(path->attrs.interconnect, "-TTL", &ttl_level, &found, path->attrs.error_message);
      if (!ok) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect spec for multicast must have -TTL <n>, where <n> is the time-to-live level. The default is 1 (same subnet).\n");
        return false;
      }
      if (!found) {
        // Set the default to something safe
        ttl_level = 1;
      }
    }
  }

  // Validate the restricted attribute values
  if ((is_sender != path->attrs.is_endpointA) || (is_recver != (!path->attrs.is_endpointA))) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The datagram interconnect requires endpoint A to be a sender and endpoint B to be a receiver.\n");
    return false;
  }
  if ((path->attrs.nbufs_AtoB < 1) && (path->attrs.nbufs_BtoA != 0)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The datagram path requires nbufs_AtoB to be >= 1 and nbufs_BtoA to be 0.\n");
    return false;
  }

  // Allocate private handle
  DatagramPath *datagram_path = calloc(1, sizeof(DatagramPath));
  if (datagram_path == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  datagram_path->socket_fd = -1;
  datagram_path->connection_failed = false;
  datagram_path->is_unicast_sender = is_unicast_sender;
  datagram_path->is_unicast_recver = is_unicast_recver;
  datagram_path->is_multicast_sender = is_multicast_sender;
  datagram_path->is_multicast_recver = is_multicast_recver;
  private_path->private = datagram_path;

  // Allocate the buffers list
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  datagram_path->send_buffer_list = calloc(nbufs_sender, sizeof(XferBuffer));
  if (datagram_path->send_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }
  datagram_path->recv_buffer_list = calloc(nbufs_recver, sizeof(XferBuffer));
  if (datagram_path->recv_buffer_list == NULL) {
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
        datagram_path->send_buffer_list[buf_index].sender_addr_alloced = true;
      } else {
        datagram_path->send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    // Recver
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr == 0) {
        datagram_path->recv_buffer_list[buf_index].recver_addr_alloced = true;
      } else {
        datagram_path->recv_buffer_list[buf_index].recver_addr = recver_addr;
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
      datagram_path->send_buffer_list[buf_index].sender_addr = (size_t)addr;
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
      datagram_path->recv_buffer_list[buf_index].recver_addr = (size_t)addr;
    }
  }

  // Create the one sided socket
  if (is_sender) {
    if (is_udp) {
      if (!socketCreateUnicastSender(ip_addr, port_number, &datagram_path->socket_fd, &datagram_path->sock_in_addr, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create UDP sender socket\n");
        goto cleanup;
      }
    } else if (is_multicast) {
      if (!socketCreateMulticastSender(ip_addr, multicast_group, port_number, disable_loopback, ttl_level, &datagram_path->socket_fd, &datagram_path->sock_in_addr, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create multicast sender socket\n");
        goto cleanup;
      }
    }
  } else {
    if (is_udp) {
      if (!socketCreateUnicastReceiver(ip_addr, port_number, allow_reuse, &datagram_path->socket_fd, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create UDP receiver socket\n");
        goto cleanup;
      }
    } else if (is_multicast) {
      if (!socketCreateMulticastReceiver(ip_addr, multicast_group, port_number, allow_reuse, &datagram_path->socket_fd, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create multicast receiver socket\n");
        goto cleanup;
      }
    }
  }

  // Set to polling mode if needed
  if (path->attrs.is_polling) {
    if (!socketSetBlocking(datagram_path->socket_fd, 0, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Could not set the UDP socket to be non blocking (i.e. polling)\n");
      socketClose(datagram_path->socket_fd);
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
void setSocketDatagramFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknSendStrided = tknSendStrided;
  private_path->tknSendTest = tknSendTest;
  private_path->tknRecv = tknRecv;
  private_path->tknRecvStrided = tknRecvStrided;
  private_path->tknDestroy = tknDestroy;
}
#endif
