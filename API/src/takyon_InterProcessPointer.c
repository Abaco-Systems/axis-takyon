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
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  size_t sender_addr;
  size_t local_recver_addr;
  size_t remote_recver_addr;
  MmapHandle local_mmap_app;    // Data memory (creator)
  MmapHandle remote_mmap_app;   // Data memory (slave, even if allocated by app)
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

  // Error checking
  if (src_offset != dest_offset) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The source offset=%lld and destination offset=%lld are not the same.\n", (unsigned long long)src_offset, (unsigned long long)dest_offset);
    return false;
  }

  // Check if waiting on a takyonIsSendFinished()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data (just a pointer passing)
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
      printf("%-15s (%s:%s) Got header: buf=%d, bytes=%lld, offset=%lld\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             recved_buffer_index,
             (unsigned long long)recved_bytes,
             (unsigned long long)recved_offset);
    }

    // Verify the received data
    SingleBuffer *recver_buffer = &buffers->recv_buffer_list[recved_buffer_index];
    uint64_t total_bytes = recved_bytes + recved_offset;
    uint64_t recver_max_bytes = path->attrs.recver_max_bytes_list[buffer_index];
    if (total_bytes > recver_max_bytes) {
      buffers->connected = false;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Receiving out of bounds data. Max is %lld bytes but trying to recv %lld plus offset of %lld\n", (unsigned long long)recver_max_bytes, (unsigned long long)recved_bytes, (unsigned long long)recved_offset);
      return false;
    }
    if (recver_buffer->got_data) {
      buffers->connected = false;
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received data for index %d twice before the first was processed\n", recved_buffer_index);
      return false;
    }

    if (recved_buffer_index == buffer_index) {
      // Got the data in the expected buffer index
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

  // Free the buffers (the posix buffers are always allocated on the recieve side)
  if (buffers->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the handle to the remote destination buffers
      if (buffers->send_buffer_list[buf_index].remote_mmap_app != NULL) {
        mmapFree(buffers->send_buffer_list[buf_index].remote_mmap_app, path->attrs.error_message);
      }
    }
    free(buffers->send_buffer_list);
  }
  if (buffers->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      // Free the local destination buffers
      if (buffers->recv_buffer_list[buf_index].recver_addr_alloced && buffers->recv_buffer_list[buf_index].local_mmap_app != NULL) {
        mmapFree(buffers->recv_buffer_list[buf_index].local_mmap_app, path->attrs.error_message);
      }
    }
    free(buffers->recv_buffer_list);
  }
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

  // Close the socket
  socketClose(buffers->socket_fd);

  // Free the shared memory resources
  freePathResources(path);
  // Free the private handle
  free(buffers);

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) done\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

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
  //   "InterProcessPointer -ID <ID> [-appAllocedRecvMmap] [-remoteMmapPrefix <name>]"
  uint32_t path_id;
  bool found;
  bool ok = argGetUInt(path->attrs.interconnect, "-ID=", &path_id, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -ID=<value>\n");
    return false;
  }
  // Check if the application will be providing the mmap memory for the remote side
  bool app_alloced_recv_mmap = argGetFlag(path->attrs.interconnect, "-appAllocedRecvMmap");
  bool has_remote_mmap_prefix = false;
  char remote_mmap_prefix[MAX_MMAP_NAME_CHARS];
  ok = argGetText(path->attrs.interconnect, "-remoteMmapPrefix=", remote_mmap_prefix, MAX_MMAP_NAME_CHARS, &has_remote_mmap_prefix, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to find interconnect flag -remoteMmapPrefix=<name>\n");
    return false;
  }
  // Create local socket name used to coordinate connection and disconnection
  char socket_name[TAKYON_MAX_INTERCONNECT_CHARS];
  snprintf(socket_name, TAKYON_MAX_INTERCONNECT_CHARS, "%s_sock_%d", MMAP_NAME_PREFIX, path_id);

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s:%s) create path (id=%d)\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           path_id);
  }

  // Verify dest addresses are all Takyon managed. This is because mmap can't map already created memory.
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  if (!app_alloced_recv_mmap) {
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      if (path->attrs.recver_addr_list[buf_index] != 0) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "All addresses in path->attrs.recver_addr_list must be NULL and will be allocated by Takyon, unless the flag '-appAllocedRecvMmap' is set which means the app allocated the receive side memory map addresses for each buffer.\n");
        return false;
      }
    }
  }

  // Verify src addresses are all Takyon managed, only if -share is active.
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    if (path->attrs.sender_addr_list[buf_index] != 0) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "All addresses in path->attrs.sender_addr_list must be NULL since both the src and dest will use the same memory\n");
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
    // Sender (can optionally be set by application)
    uint64_t sender_bytes = path->attrs.sender_max_bytes_list[buf_index];
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
    // Recver (this should not be set by application, unless the app specifically set the flag -appAllocedRecvMmap)
    if (app_alloced_recv_mmap) {
      uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
      if (recver_bytes > 0) {
        size_t recver_addr = path->attrs.recver_addr_list[buf_index];
        if (recver_addr == 0) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "attrs.recver_addr_list[%d]=0, but the flag '-appAllocedRecvMmap' has been set which means the application must allocate all the buffers with memory mapped addresses.\n", buf_index);
          goto cleanup;
        } else {
          buffers->recv_buffer_list[buf_index].local_recver_addr = recver_addr;
        }
      }
    } else {
      buffers->recv_buffer_list[buf_index].recver_addr_alloced = true;
    }
  }

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
  // Sender and recevier must have same size since they are shared
  for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
    uint64_t max_bytes = path->attrs.is_endpointA ? path->attrs.sender_max_bytes_list[i] : path->attrs.recver_max_bytes_list[i];
    if (!socketSwapAndVerifyUInt64(buffers->socket_fd, max_bytes, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message,
                          "Both endpoints, sharing A to B buffer, are using a different values for sender_max_bytes_list[%d]=%lld and recver_max_bytes_list[%d]\n",
                          i, (unsigned long long)max_bytes, i);
      goto cleanup;
    }
    // This is shared memory so the local sender and remote receiver are the same
    if (path->attrs.is_endpointA) buffers->send_buffer_list[i].remote_max_recver_bytes = path->attrs.sender_max_bytes_list[i];
  }
  for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
    uint64_t max_bytes = path->attrs.is_endpointA ? path->attrs.recver_max_bytes_list[i] : path->attrs.sender_max_bytes_list[i];
    if (!socketSwapAndVerifyUInt64(buffers->socket_fd, max_bytes, private_path->path_create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message,
                          "Both endpoints, sharing B to A buffer, are using a different values for recver_max_bytes_list[%d]=%lld and sender_max_bytes_list[%d]\n",
                          i, (unsigned long long)max_bytes, i);
      goto cleanup;
    }
    // This is shared memory so the local sender and remote receiver are the same
    if (!path->attrs.is_endpointA) buffers->send_buffer_list[i].remote_max_recver_bytes = path->attrs.sender_max_bytes_list[i];
  }
  
  // Create local recver memory (this is a posix memory map)
  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) Create shared destination resources\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Application memory
    if (app_alloced_recv_mmap) {
      // Passed in by the application
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      buffers->recv_buffer_list[buf_index].local_recver_addr = recver_addr;
    } else {
      // Takyon needs to allocate
      uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
      void *addr = NULL;
      if (recver_bytes > 0) {
        char local_mmap_name[MAX_MMAP_NAME_CHARS];
        snprintf(local_mmap_name, MAX_MMAP_NAME_CHARS, "%s_%s_app_%d_%lld_%d", MMAP_NAME_PREFIX, path->attrs.is_endpointA ? "c" : "s", buf_index, (unsigned long long)recver_bytes, path_id);
        if (!mmapAlloc(local_mmap_name, recver_bytes, &addr, &buffers->recv_buffer_list[buf_index].local_mmap_app, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create shared app memory: %s\n", local_mmap_name);
          goto cleanup;
        }
      }
      path->attrs.recver_addr_list[buf_index] = (size_t)addr;
      buffers->recv_buffer_list[buf_index].local_recver_addr = (size_t)addr;
    }
  }

  // Socket round trip, to make sure the remote memory handle is the latest and not stale
  if (!socketBarrier(path->attrs.is_endpointA, buffers->socket_fd, BARRIER_INIT_STEP2, private_path->path_create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 2\n");
    goto cleanup;
  }

  // Get the remote destination address
  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%s (%s:%s) Get remote memory addresses\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    uint64_t remote_recver_bytes = buffers->send_buffer_list[buf_index].remote_max_recver_bytes;
    char remote_mmap_name[MAX_MMAP_NAME_CHARS];
    // Application memory
    if (has_remote_mmap_prefix) {
      snprintf(remote_mmap_name, MAX_MMAP_NAME_CHARS, "%s%d", remote_mmap_prefix, buf_index);
    } else {
      snprintf(remote_mmap_name, MAX_MMAP_NAME_CHARS, "%s_%s_app_%d_%lld_%d", MMAP_NAME_PREFIX, path->attrs.is_endpointA ? "s" : "c", buf_index, (unsigned long long)remote_recver_bytes, path_id);
    }
    void *addr = NULL;
    if (remote_recver_bytes > 0) {
      bool timed_out;
      bool success = mmapGetTimed(remote_mmap_name, remote_recver_bytes, &addr, &buffers->send_buffer_list[buf_index].remote_mmap_app, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
      if ((!success) || timed_out) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get handle to remote shared app memory: %s\n", remote_mmap_name);
        goto cleanup;
      }
    }
    buffers->send_buffer_list[buf_index].remote_recver_addr = (size_t)addr;
    // Need the sender addr to be the same as the destination address
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
  freePathResources(path);
  free(buffers);
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
