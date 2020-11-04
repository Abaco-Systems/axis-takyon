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
//   This is a Takyon interface to the inter-process memcpy() interface.
//   Local sockets are used to coordinate the connection, disconnection, and
//   synchronization.
//   The destination buffers are shared memory maps.
//   Sender and receiver share the same buffers (a pointer).
// -----------------------------------------------------------------------------

#include "takyon_private.h"

// Used by the socket based initilize and finalize communication
#define XFER_COMMAND       777666
#define BARRIER_INIT_STEP1 777222
#define BARRIER_INIT_STEP2 888222
#define BARRIER_INIT_STEP3 999222
#define BARRIER_FINALIZE   555222
#define MMAP_NAME_PREFIX "TknIPP"

typedef struct {
  uint64_t remote_max_recver_bytes; // Only used by the sender
  bool recver_addr_alloced;
#ifdef WITH_CUDA
  bool recver_is_cuda_addr;
  cudaEvent_t recver_cuda_event;
#endif
  size_t sender_addr;
  size_t local_recver_addr;
  char local_recver_mmap_name[MAX_MMAP_NAME_CHARS];
#ifdef WITH_CUDA
  bool remote_recver_is_cuda;
  cudaEvent_t remote_recver_cuda_event;
#endif
  size_t remote_recver_addr;
  MmapHandle local_mmap_handle;    // Data memory (creator)
  MmapHandle remote_mmap_handle;   // Data memory (remote, even if allocated by app)
  bool send_started;
  bool got_data;
  uint64_t recved_bytes;
  uint64_t recved_offset;
} SingleBuffer;

typedef struct {
  TakyonSocket socket_fd;
  bool connected;
  SingleBuffer *send_buffer_list;
  SingleBuffer *recv_buffer_list;
} PathBuffers;

GLOBAL_VISIBILITY bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->send_buffer_list[buffer_index];

  // Verify connection is good
  // IMPORTANT: this is not protected in a mutex, but should be fine since the send will not go to sleep waiting in a conditional variable
  if (!buffers->connected) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Since the source and dest buffers are the same buffer, make sure the offsets are the same
  if (src_offset != dest_offset) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The source offset=%ju and destination offset=%ju are not the same.\n", src_offset, dest_offset);
    return false;
  }

  // Check if waiting on a takyonIsSendFinished()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data (just a pointer passing so no actual transfer)
  if (bytes > 0) {
#ifdef WITH_CUDA
    // IMPORTANT: cudaMemcpy() is not synchronous of the remote memory is GPU
    // Notify remote side that the cudaMemcpy() transfer is complete on the sender side
    if (buffer->remote_recver_is_cuda) {
      if (!cudaEventNotify(&buffers->send_buffer_list[buffer_index].remote_recver_cuda_event, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to notify recver that transfer is complete via CUDA event\n");
        return false;
      }
    }
#endif
  }
  buffer->send_started = true;

  // Send the header and signal together via the socket
  uint64_t header[4];
  header[0] = XFER_COMMAND;
  header[1] = buffer_index;
  header[2] = bytes;
  header[3] = dest_offset;
  if (!socketSend(buffers->socket_fd, header, sizeof(header), path->attrs.is_polling, private_path->send_start_timeout_ns, private_path->send_start_timeout_ns, timed_out_ret, path->attrs.error_message)) {
    buffers->connected = false;
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to transfer header\n");
    return false;
  }
  if ((private_path->send_start_timeout_ns >= 0) && (*timed_out_ret == true)) {
    // Timed out but no data was transfered yet
    return true;
  }

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
  SingleBuffer *buffer = &buffers->send_buffer_list[buffer_index];

  // Verify connection is good
  // IMPORTANT: this is not protected in a mutex, but should be fine since the send will not go to sleep waiting in a conditional variable
  if (!buffers->connected) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Verify no double writes
  if (!buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "takyonIsSendFinished() was called, but a prior takyonSend() was not called on buffer %d\n", buffer_index);
    return false;
  }

  // Since memcpy can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;
  return true;
}

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->recv_buffer_list[buffer_index];

  // Verify connection is good
  if (!buffers->connected) {
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
    // NOTE: If the header arrives, this means the data was already memcpy'ed over
    uint64_t header[4];
    if (!socketRecv(buffers->socket_fd, header, sizeof(header), path->attrs.is_polling, private_path->recv_start_timeout_ns, private_path->recv_start_timeout_ns, timed_out_ret, path->attrs.error_message)) {
      buffers->connected = false;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to receive header\n");
      return false;
    }
    if ((private_path->recv_start_timeout_ns >= 0) && (*timed_out_ret == true)) {
      // Timed out but no data was transfered yet
      return true;
    }
    if (header[0] != XFER_COMMAND) { 
      buffers->connected = false;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "got unexpected header\n");
      return false;
    }
    int recved_buffer_index = (int)header[1];
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    if (recved_buffer_index >= nbufs_recver) {
      buffers->connected = false;
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

    // Verify the received data
    SingleBuffer *recver_buffer = &buffers->recv_buffer_list[recved_buffer_index];
    uint64_t total_bytes = recved_bytes + recved_offset;
    uint64_t recver_max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
    if (total_bytes > recver_max_bytes) {
      buffers->connected = false;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Receiving out of bounds data. Max is %ju bytes but trying to recv %ju plus offset of %ju\n", recver_max_bytes, recved_bytes, recved_offset);
      return false;
    }
    if (recver_buffer->got_data) {
      buffers->connected = false;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received data for index %d twice before the first was processed\n", recved_buffer_index);
      return false;
    }

#ifdef WITH_CUDA
    // The header was sent by the socket, but the CUDA transfer might not yet be complete, need to wait for the CUDA event
    if (recved_bytes > 0 && recver_buffer->recver_is_cuda_addr) {
      if (!cudaEventWait(&recver_buffer->recver_cuda_event, path->attrs.error_message)) {
        buffers->connected = false;
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to wait for the CUDA event on buffer %d, which notifies if the sender completed the transfer.\n", recved_buffer_index);
        return false;
      }
    }
#endif

    if (recved_buffer_index == buffer_index) {
      // Got the data in the expected buffer index
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

static bool freePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Free the send side buffers (the posix buffers are always allocated on the recieve side)
  if (buffers->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the handle to the remote destination buffers
      if (buffers->send_buffer_list[buf_index].remote_recver_addr != 0) {
        if (buffers->send_buffer_list[buf_index].remote_mmap_handle != NULL) {
          if (!mmapFree(buffers->send_buffer_list[buf_index].remote_mmap_handle, path->attrs.error_message)) {
            TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to release the remote buffer mapping\n");
            return false;
          }
        }
#ifdef WITH_CUDA
        if (buffers->send_buffer_list[buf_index].remote_recver_is_cuda) {
          void *remote_cuda_addr = (void *)buffers->send_buffer_list[buf_index].remote_recver_addr;
          if (!cudaReleaseRemoteIpcMappedAddr(remote_cuda_addr, path->attrs.error_message)) {
            TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to release the remote CUDA buffer mapping\n");
            return false;
          }
        }
#endif
      }
      // IMPORTANT: No send buffers to free, since there are only dest buffers
    }
    free(buffers->send_buffer_list);
  }

  // Free the recv side buffers (the posix buffers are always allocated on the recieve side)
  if (buffers->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
#ifdef WITH_CUDA
      // Free CUDA events for takyon managed and application created CUDA memory
      uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
      if (recver_bytes > 0 && buffers->recv_buffer_list[buf_index].recver_is_cuda_addr) {
        if (!cudaEventFree(&buffers->recv_buffer_list[buf_index].recver_cuda_event, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to free recv CUDA event\n");
          return false;
        }
      }
#endif
      // Free the local destination buffers
      if (buffers->recv_buffer_list[buf_index].recver_addr_alloced) {
        if (buffers->recv_buffer_list[buf_index].local_mmap_handle != NULL) {
          if (!mmapFree(buffers->recv_buffer_list[buf_index].local_mmap_handle, path->attrs.error_message)) {
            TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to release the local MMAP buffer mapping\n");
            return false;
          }
        }
#ifdef WITH_CUDA
        if (buffers->recv_buffer_list[buf_index].recver_is_cuda_addr && buffers->recv_buffer_list[buf_index].local_recver_addr != 0) {
          if (!cudaMemoryFree((void *)buffers->recv_buffer_list[buf_index].local_recver_addr, path->attrs.error_message)) {
            TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to free recv CUDA buffer\n");
            return false;
          }
        }
#endif
      }
    }
    free(buffers->recv_buffer_list);
  }

  // Free the private handle
  free(buffers);

  return true;
}

GLOBAL_VISIBILITY bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  bool graceful_disconnect_ok = true;
  if (buffers->connected) {
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
    if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_FINALIZE, private_path->path_destroy_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run finalize barrier part 1\n");
      graceful_disconnect_ok = false;
    }
  }

  // Sleep here or else remote side might error out from a disconnect message while the remote process completing it's last transfer and waiting for any TCP ACKs to validate a complete transfer
  clockSleepYield(MICROSECONDS_TO_SLEEP_BEFORE_DISCONNECTING);

  // Close the socket
  socketClose(buffers->socket_fd);

  // Free the shared memory resources
  if (!freePathMemoryResources(path)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Free memory resources failed\n");
    graceful_disconnect_ok = false;
  }

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) done\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  return graceful_disconnect_ok;
}

static bool sendRecverBufferInfo(TakyonPath *path, int nbufs_recver) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      bool is_cuda_addr = false;
      bool timed_out;
      bool ok;
#ifdef WITH_CUDA
      is_cuda_addr = buffers->recv_buffer_list[buf_index].recver_is_cuda_addr;
      ok = socketSend(buffers->socket_fd, &is_cuda_addr, sizeof(bool), false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
      if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver is_cuda_addr for buffer %d\n", buf_index); return false; }
      if (is_cuda_addr) {
        // Send over address handle
        size_t addr = buffers->recv_buffer_list[buf_index].local_recver_addr;
        cudaIpcMemHandle_t ipc_map;
        ok = cudaCreateIpcMapFromLocalAddr((void *)addr, &ipc_map, path->attrs.error_message);
        if (!ok) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to map the CUDA recver memory for buffer %d\n", buf_index); return false; }
        ok = socketSend(buffers->socket_fd, &ipc_map, sizeof(cudaIpcMemHandle_t), false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver CUDA memory map info for buffer %d\n", buf_index); return false; }
        // Send over event handle (needed even for CPU addresses)
        cudaEvent_t *recver_cuda_event = &buffers->recv_buffer_list[buf_index].recver_cuda_event;
        cudaIpcEventHandle_t event_ipc_map;
        ok = cudaCreateIpcMapFromLocalEvent(recver_cuda_event, &event_ipc_map, path->attrs.error_message);
        if (!ok) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to map the CUDA event for buffer %d\n", buf_index); return false; }
        ok = socketSend(buffers->socket_fd, &event_ipc_map, sizeof(cudaIpcEventHandle_t), false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver CUDA event map info for buffer %d\n", buf_index); return false; }
      }
#endif
      if (!is_cuda_addr) {
        char *name = buffers->recv_buffer_list[buf_index].local_recver_mmap_name;
        ok = socketSend(buffers->socket_fd, name, MAX_MMAP_NAME_CHARS, false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver mmap name for buffer %d\n", buf_index); return false; }
      }
    }
  }
  return true;
}

static bool recvRecverBufferInfo(TakyonPath *path, int nbufs_sender) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    buffers->send_buffer_list[buf_index].remote_recver_addr = 0;
    uint64_t remote_recver_bytes = buffers->send_buffer_list[buf_index].remote_max_recver_bytes;
    if (remote_recver_bytes > 0) {
      bool is_cuda_addr = false;
      bool timed_out;
      bool ok;
#ifdef WITH_CUDA
      ok = socketRecv(buffers->socket_fd, &is_cuda_addr, sizeof(bool), false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
      if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver is_cuda_addr for buffer %d\n", buf_index); return false; }
      buffers->send_buffer_list[buf_index].remote_recver_is_cuda = is_cuda_addr;
      if (is_cuda_addr) {
        // Wait to recv the CUDA memory map handle
        cudaIpcMemHandle_t remote_ipc_map;
        ok = socketRecv(buffers->socket_fd, &remote_ipc_map, sizeof(cudaIpcMemHandle_t), false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver CUDA memory map info for buffer %d\n", buf_index); return false; }
        void *remote_cuda_addr = cudaGetRemoteAddrFromIpcMap(remote_ipc_map, path->attrs.error_message);
        if (!ok) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get the remote CUDA recver memory for buffer %d\n", buf_index); return false; }
        buffers->send_buffer_list[buf_index].remote_recver_addr = (size_t)remote_cuda_addr;
        // Wait to recv the CUDA event map handle (even for CPU addresses)
        cudaIpcEventHandle_t remote_event_ipc_map;
        ok = socketRecv(buffers->socket_fd, &remote_event_ipc_map, sizeof(cudaIpcEventHandle_t), false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver CUDA event map info for buffer %d\n", buf_index); return false; }
        ok = cudaGetRemoteEventFromIpcMap(remote_event_ipc_map, &buffers->send_buffer_list[buf_index].remote_recver_cuda_event, path->attrs.error_message);
        if (!ok) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get the remote CUDA recver event for buffer %d\n", buf_index); return false; }
        // Verbosity
        if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
          printf("%-15s (%s:%s) Got remote CUDA addr and event for recv buffer %d\n", __FUNCTION__, path->attrs.is_endpointA ? "A" : "B", path->attrs.interconnect, buf_index);
        }
      }
#endif
      if (!is_cuda_addr) {
        char remote_mmap_name[MAX_MMAP_NAME_CHARS];
        ok = socketRecv(buffers->socket_fd, remote_mmap_name, MAX_MMAP_NAME_CHARS, false, private_path->path_create_timeout_ns, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver mmap name for buffer %d\n", buf_index); return false; }
        // Verbosity
        if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
          printf("%-15s (%s:%s) Getting remote MMAP '%s' addr to recv buffer %d\n", __FUNCTION__, path->attrs.is_endpointA ? "A" : "B", path->attrs.interconnect, remote_mmap_name, buf_index);
        }
        void *remote_addr = NULL;
        ok = mmapGetTimed(remote_mmap_name, remote_recver_bytes, &remote_addr, &buffers->send_buffer_list[buf_index].remote_mmap_handle, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
        if (!ok || timed_out) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get handle to remote shared app memory: %s\n", remote_mmap_name); return false; }
        buffers->send_buffer_list[buf_index].remote_recver_addr = (size_t)remote_addr;
      }
    }
  }
  return true;
}

GLOBAL_VISIBILITY bool tknCreate(TakyonPath *path) {
  // Supported formats:
  //   "InterProcessPointer -ID=<ID> [-recverAddrMmapNamePrefix=<name>]"
  // WITH_CUDA extras:
  //   [-destCudaDeviceId=<id>]   If Takyon allocates the destination buffers, then use cudaMalloc() on CUDA device <id>. If not specified, a CPU allocation is done if not pre-allocated.

  // Get the path ID
  uint32_t path_id;
  bool found;
  bool ok = argGetUInt(path->attrs.interconnect, "-ID=", &path_id, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -ID=<value>\n");
    return false;
  }

  // Create local socket name used to coordinate connection, synchronization and disconnection
  char socket_name[TAKYON_MAX_INTERCONNECT_CHARS];
  snprintf(socket_name, TAKYON_MAX_INTERCONNECT_CHARS, "%s_sock_%d", MMAP_NAME_PREFIX, path_id);

  // Check if the application will be providing the mmap memory for the remote side
  bool has_remote_mmap_prefix = false;
  char remote_mmap_prefix[TAKYON_MAX_INTERCONNECT_CHARS];
  ok = argGetText(path->attrs.interconnect, "-recverAddrMmapNamePrefix=", remote_mmap_prefix, TAKYON_MAX_INTERCONNECT_CHARS, &has_remote_mmap_prefix, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Invalid interconnect flag format: expecting -recverAddrMmapNamePrefix=<name>\n");
    return false;
  }

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s:%s) create path (id=%d)\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           path_id);
  }

  // Get the cuda devices IDs for memory allocation
#ifdef WITH_CUDA
  int dest_cuda_device_id = -1;
  ok = argGetInt(path->attrs.interconnect, "-destCudaDeviceId=", &dest_cuda_device_id, &found, path->attrs.error_message);
  if (!ok || (found && dest_cuda_device_id<0)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The optional argument '-destCudaDeviceId=' was specified. It must be an integer >= 0 but was set to %d\n", dest_cuda_device_id);
    return false;
  }
#endif

  // Verify src addresses are all NULL since they wont be used
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    if (path->attrs.sender_addr_list[buf_index] != 0) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "All addresses in path->attrs.sender_addr_list must be NULL since both the src and dest will use the same memory buffer pointers\n");
      return false;
    }
  }

  // Allocate private handle
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = calloc(1, sizeof(PathBuffers));
  if (buffers == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  private_path->private_data = buffers;

  // Allocate the buffers
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

  // Fill in some initial sender fields
  // IMPORTANT: nothing to fill in yet with the send buffers since they are all NULL

  // Fill in some initial recver fields
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Recver
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr == 0) {
        buffers->recv_buffer_list[buf_index].recver_addr_alloced = true;
      } else {
        buffers->recv_buffer_list[buf_index].local_recver_addr = recver_addr;
        bool is_cpu_mem = true;
#ifdef WITH_CUDA
        bool is_cuda_addr;
        if (!isCudaAddress((void *)recver_addr, &is_cuda_addr, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to determine if app allocated addr, at attrs.recver_addr_list[%d], is CUDA or CPU memory.\n", buf_index);
          goto cleanup;
        }
        buffers->recv_buffer_list[buf_index].recver_is_cuda_addr = is_cuda_addr;
        is_cpu_mem = !is_cuda_addr;
#endif
        if (is_cpu_mem && !has_remote_mmap_prefix) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "attrs.recver_addr_list[%d] supplies an application defined address, but the flag '-recverAddrMmapNamePrefix=<name>' has not been set. This needs to be set so the remote endpoint knows the name of the memory map.\n", buf_index);
          goto cleanup;
        }
        // Record the app allocated mmap name
        int prefix_length = (int)strlen(remote_mmap_prefix);
        snprintf(buffers->recv_buffer_list[buf_index].local_recver_mmap_name, MAX_MMAP_NAME_CHARS, "%.*s%d", prefix_length, remote_mmap_prefix, buf_index);
      }
    }
  }

  // Create local sender memory
  // IMPORTANT: not needed since only dest buffers exists

  // Create local socket to synchronize the creation
  if (path->attrs.is_endpointA) {
    if (!socketCreateLocalClient(socket_name, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local client socket\n");
      goto cleanup;
    }
  } else {
    if (!socketCreateLocalServer(socket_name, &buffers->socket_fd, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local server socket\n");
      goto cleanup;
    }
  }

  // Socket round trip, to make sure the socket is really connected
  if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_INIT_STEP1, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 1\n");
    goto cleanup;
  }

  // Verify endpoints have the same attributes
  if (!socketSwapAndVerifyInt(buffers->socket_fd, path->attrs.nbufs_AtoB, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_AtoB: (this=%d)\n", path->attrs.nbufs_AtoB);
    goto cleanup;
  }
  if (!socketSwapAndVerifyInt(buffers->socket_fd, path->attrs.nbufs_BtoA, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_BtoA: (this=%d)\n", path->attrs.nbufs_BtoA);
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

  // This is shared memory so verify the local sender and remote receiver are the same size
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    if (buffers->send_buffer_list[buf_index].remote_max_recver_bytes != path->attrs.sender_max_bytes_list[buf_index]) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Endpoints, sharing a single buffer, are using a different values for sender_max_bytes_list[%d]=%ju and remote recver_max_bytes_list[%d]=ju\n",
                          buf_index, path->attrs.sender_max_bytes_list[buf_index], buf_index, buffers->send_buffer_list[buf_index].remote_max_recver_bytes);
      goto cleanup;
    }
  }
  
  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) Create shared destination resources\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  // Create local recver memory (this is a posix memory map)
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0 && path->attrs.recver_addr_list[buf_index] == 0) {
      // Takyon needs to allocate
      void *addr = NULL;
      bool alloc_mmap = true;
#ifdef WITH_CUDA
      buffers->recv_buffer_list[buf_index].recver_is_cuda_addr = (dest_cuda_device_id >= 0);
      if (buffers->recv_buffer_list[buf_index].recver_is_cuda_addr) {
        alloc_mmap = false;
        addr = cudaMemoryAlloc(dest_cuda_device_id, recver_bytes, path->attrs.error_message);
        if (addr == NULL) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of GPU memory\n"); goto cleanup; }
      }
#endif
      if (alloc_mmap) {
        char local_mmap_name[MAX_MMAP_NAME_CHARS];
        snprintf(local_mmap_name, MAX_MMAP_NAME_CHARS, "%s_%s_app_%d_%ju_%d", MMAP_NAME_PREFIX, path->attrs.is_endpointA ? "c" : "s", buf_index, recver_bytes, path_id);
        if (!mmapAlloc(local_mmap_name, recver_bytes, &addr, &buffers->recv_buffer_list[buf_index].local_mmap_handle, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create shared app memory: %s\n", local_mmap_name);
          goto cleanup;
        }
        // Record the mmap name
        snprintf(buffers->recv_buffer_list[buf_index].local_recver_mmap_name, MAX_MMAP_NAME_CHARS, "%s", local_mmap_name);
      }
      path->attrs.recver_addr_list[buf_index] = (size_t)addr;
      buffers->recv_buffer_list[buf_index].local_recver_addr = (size_t)addr;
    }
  }

#ifdef WITH_CUDA
  // Create the CUDA event handles for all GPU recv buffers
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0 && buffers->recv_buffer_list[buf_index].recver_is_cuda_addr) {
      cudaEvent_t *recver_cuda_event = &buffers->recv_buffer_list[buf_index].recver_cuda_event;
      ok = cudaEventAlloc(dest_cuda_device_id, recver_cuda_event, path->attrs.error_message);
      if (!ok) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create the CUDA event for buffer %d\n", buf_index); goto cleanup; }
    }
  }
#endif

  // Socket round trip, to make sure the remote memory handle is the latest and not stale
  if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_INIT_STEP2, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 2\n");
    goto cleanup;
  }

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) Get remote memory addresses\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  // Get the information about each remote recv buffer
  // Make sure each sender knows the remote recver buffer sizes
  if (path->attrs.is_endpointA) {
    // Step 1: Endpoint A: send to B
    if (!sendRecverBufferInfo(path, nbufs_recver)) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send A->B recver buffer info\n"); goto cleanup; }
    // Step 4: Endpoint A: recv from B
    if (!recvRecverBufferInfo(path, nbufs_sender)) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv A->B remote recver buffer info\n"); goto cleanup; }
  } else {
    // Step 2: Endpoint B: recv from A
    if (!recvRecverBufferInfo(path, nbufs_sender)) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv B->A remote recver buffer info\n"); goto cleanup; }
    // Step 3: Endpoint B: send to A
    if (!sendRecverBufferInfo(path, nbufs_recver)) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send B->A recver buffer info\n"); goto cleanup; }
  }

  // No set the sender address to the same as the remote recver address
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    size_t addr = buffers->send_buffer_list[buf_index].remote_recver_addr;
    path->attrs.sender_addr_list[buf_index] = (size_t)addr;
    buffers->send_buffer_list[buf_index].sender_addr = (size_t)addr;
  }

  // Socket round trip, to make sure the create is completed on both sides and the remote side has not had a change to destroy any of the resources
  if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_INIT_STEP3, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 3\n");
    goto cleanup;
  }

  // Set to polling mode if needed
  if (path->attrs.is_polling) {
    if (!socketSetBlocking(buffers->socket_fd, 0, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Could not set the socket to be non blocking (i.e. polling)\n");
      socketClose(buffers->socket_fd);
      goto cleanup;
    }
  }

  buffers->connected = true;

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) Completed connection\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  (void)freePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setInterProcessPointerFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
