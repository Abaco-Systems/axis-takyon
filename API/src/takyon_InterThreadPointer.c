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
//   This is a Takyon interface to the inter-thread pointer sharing interface.
//   Both endpoints share a common buffer, and use pointers to coordinate the
//   instant transfer.
//   Posix mutex and conditional variables are used for the synchronization.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define THREAD_MANAGER_ID 24 // This must be different from the other interconnects that use the thread manager

typedef struct {
  uint64_t sender_max_bytes;
  uint64_t recver_max_bytes;
  bool addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
  bool send_started;
  volatile bool got_data;
  uint64_t bytes_recved;
  uint64_t offset_recved;
} SingleBuffer;

typedef struct {
  InterThreadManagerItem *shared_thread_item;
  SingleBuffer *send_buffer_list;
  SingleBuffer *recv_buffer_list;
} PathBuffers;

GLOBAL_VISIBILITY bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->send_buffer_list[buffer_index];
  InterThreadManagerItem *shared_item = buffers->shared_thread_item;

  // Lock the mutex now since many of the variables come from the remote side
  pthread_mutex_lock(&shared_item->mutex);

  // Verify connection is good
  if (shared_item->connection_broken) {
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Error checking
  if (buffer->send_started) {
    interThreadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }
  if (src_offset != dest_offset) {
    interThreadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The source offset=%lld and destination offset=%lld are not the same.\n", (unsigned long long)src_offset, (unsigned long long)dest_offset);
    return false;
  }

  // Transfer the data
  buffer->send_started = true;

  // Set some sync flags, and signal the receiver
  TakyonPath *remote_path = path->attrs.is_endpointA ? shared_item->pathB : shared_item->pathA;
  TakyonPrivatePath *remote_private_path = (TakyonPrivatePath *)remote_path->private_path;
  PathBuffers *remote_buffers = (PathBuffers *)remote_private_path->private_data;
  SingleBuffer *remote_buffer = &remote_buffers->recv_buffer_list[buffer_index];
  remote_buffer->bytes_recved = bytes;
  remote_buffer->offset_recved = dest_offset;
  remote_buffer->got_data = true;
  if (!path->attrs.is_polling) {
    // Unlock mutex and signal receiver
    pthread_cond_signal(&shared_item->cond);
  }

  // Done with remote variables
  pthread_mutex_unlock(&shared_item->mutex);

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
  InterThreadManagerItem *shared_item = buffers->shared_thread_item;

  // Lock the mutex now since many of the variables come from the remote side
  pthread_mutex_lock(&shared_item->mutex);

  // Verify connection is good
  if (shared_item->connection_broken) {
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Verify no double writes
  if (!buffer->send_started) {
    interThreadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "takyonIsSendFinished() was called, but a prior takyonSend() was not called on buffer %d\n", buffer_index);
    return false;
  }

  // Since this interconnect can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;

  pthread_mutex_unlock(&shared_item->mutex);

  return true;
}

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->recv_buffer_list[buffer_index];
  int64_t time1 = 0;
  InterThreadManagerItem *shared_item = buffers->shared_thread_item;

  if (path->attrs.is_polling) {
    time1 = clockTimeNanoseconds();
    // IMPORTANT: The mutex is unlocked while spinning waiting for data or the connection is broke, but this is no longer atomic,
    //            but should be fine since both are single intergers and mutually exclusive
  } else {
    // Lock the mutex now since many of the variables come from the remote side
    pthread_mutex_lock(&shared_item->mutex);
  }

  // Verbosity
  if (!buffer->got_data && (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE)) {
    printf("%-15s (%s:%s) waiting for data on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // See if the data has been sent
  while (!buffer->got_data && !shared_item->connection_broken) {
    // No data yet, so wait for data until the timeout occurs
    if (path->attrs.is_polling) {
      // Check timeout
      if (private_path->recv_start_timeout_ns == TAKYON_NO_WAIT) {
        // No timeout, so return now
        if (timed_out_ret != NULL) *timed_out_ret = true;
        return true;
      } else if (private_path->recv_start_timeout_ns >= 0) {
        // Hit the timeout without data, time to return
        int64_t time2 = clockTimeNanoseconds();
        int64_t diff = time2 - time1;
        if (diff > private_path->recv_start_timeout_ns) {
          if (timed_out_ret != NULL) *timed_out_ret = true;
          return true;
        }
      }
    } else {
      // Sleep while waiting for data
      bool timed_out;
      bool suceeded = threadCondWait(&shared_item->mutex, &shared_item->cond, private_path->recv_start_timeout_ns, &timed_out, path->attrs.error_message);
      if (!suceeded) {
        interThreadManagerMarkConnectionAsBad(shared_item);
        pthread_mutex_unlock(&shared_item->mutex);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to wait for data\n");
        return false;
      }
      if (timed_out) {
        pthread_mutex_unlock(&shared_item->mutex);
        if (timed_out_ret != NULL) *timed_out_ret = true;
        return true;
      }
    }
  }

  // Verify connection is good
  if (shared_item->connection_broken) {
    if (!path->attrs.is_polling) pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE) {
    printf("%-15s (%s:%s) Received %lld bytes, at offset %lld, on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           (unsigned long long)buffer->bytes_recved,
           (unsigned long long)buffer->offset_recved,
           buffer_index);
  }

  if (bytes_ret != NULL) *bytes_ret = buffer->bytes_recved;
  if (offset_ret != NULL) *offset_ret = buffer->offset_recved;
  buffer->got_data = false;
  buffer->bytes_recved = 0;
  buffer->offset_recved = 0;

  // Unlock
  if (!path->attrs.is_polling) pthread_mutex_unlock(&shared_item->mutex);

  return true;
}

static void freePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;

  // Free buffer resources (managed by side A)
  if (path->attrs.is_endpointA) {
    // A to B allocations
    for (int buf_index=0; buf_index<path->attrs.nbufs_AtoB; buf_index++) {
      SingleBuffer *this_send_buffer = &buffers->send_buffer_list[buf_index];
      if (this_send_buffer->addr_alloced && (this_send_buffer->sender_addr != 0)) {
        memoryFree((void *)this_send_buffer->sender_addr, path->attrs.error_message);
      }
    }

    // B to A allocations
    for (int buf_index=0; buf_index<path->attrs.nbufs_BtoA; buf_index++) {
      SingleBuffer *this_recv_buffer = &buffers->recv_buffer_list[buf_index];
      if (this_recv_buffer->addr_alloced && (this_recv_buffer->recver_addr != 0)) {
        memoryFree((void *)this_recv_buffer->recver_addr, path->attrs.error_message);
      }
    }
  }

  free(buffers->send_buffer_list);
  free(buffers->recv_buffer_list);

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

  // Do a coordinated disconnect
  bool graceful_disconnect_ok = true;
  if (!interThreadManagerDisconnect(path, buffers->shared_thread_item)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Thread disconnect failed\n");
    graceful_disconnect_ok = false;
  }

  // Free path memory resources
  freePathMemoryResources(path);
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

  // Call this to make sure the mutex manager is ready to coordinate: This can be called multiple times, but it's guaranteed to atomically run only the first time called.
  if (!interThreadManagerInit()) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to start the mutex manager\n");
    return false;
  }

  // Supported formats:
  //   "InterThreadPointer -ID=<ID>"
  uint32_t path_id;
  bool found;
  bool ok = argGetUInt(path->attrs.interconnect, "-ID=", &path_id, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -ID=<ID>\n");
    return false;
  }

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("%-15s (%s:%s) create path (id=%u)\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           path_id);
  }

  // Create the path resources
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;

  // Allocate private handle
  PathBuffers *buffers = calloc(1, sizeof(PathBuffers));
  if (buffers == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
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
    // Sender
    uint64_t sender_bytes = path->attrs.sender_max_bytes_list[buf_index];
    if (sender_bytes > 0) {
      size_t sender_addr = path->attrs.sender_addr_list[buf_index];
      if (sender_addr != 0) {
        buffers->send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
    buffers->send_buffer_list[buf_index].sender_max_bytes = sender_bytes;
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Recver
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr != 0) {
        buffers->recv_buffer_list[buf_index].recver_addr = recver_addr;
      }
    }
    buffers->recv_buffer_list[buf_index].recver_max_bytes = recver_bytes;
  }

  // Connect to the remote thread
  InterThreadManagerItem *item = interThreadManagerConnect(THREAD_MANAGER_ID, path_id, path);
  if (item == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to connect to remote thread\n");
    goto cleanup;
  }
  buffers->shared_thread_item = item;

  // Verify endpoints have the same attributes
  TakyonPath *remote_path = path->attrs.is_endpointA ? item->pathB : item->pathA;
  if (path->attrs.is_polling != remote_path->attrs.is_polling) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->is_polling\n");
    goto cleanup;
  }
  if (path->attrs.nbufs_AtoB != remote_path->attrs.nbufs_AtoB) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_AtoB: (%d, %d)\n", path->attrs.nbufs_AtoB, remote_path->attrs.nbufs_AtoB);
    goto cleanup;
  }
  if (path->attrs.nbufs_BtoA != remote_path->attrs.nbufs_BtoA) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_BtoA: (%d, %d)\n", path->attrs.nbufs_BtoA, remote_path->attrs.nbufs_BtoA);
    goto cleanup;
  }

  // Make sure each sender and remote recver buffers are the same size, but only if shared memory
  for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
    if (path->attrs.sender_max_bytes_list[i] != remote_path->attrs.recver_max_bytes_list[i]) {
      TAKYON_RECORD_ERROR(path->attrs.error_message,
                          "Both endpoints, sharing A to B buffer, are using a different values for sender_max_bytes_list[%d]=%lld and recver_max_bytes_list[%d]=%lld\n",
                          i, (unsigned long long)path->attrs.sender_max_bytes_list[i],
                          i, (unsigned long long)remote_path->attrs.recver_max_bytes_list[i]);
      goto cleanup;
    }
  }
  for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
    if (path->attrs.recver_max_bytes_list[i] != remote_path->attrs.sender_max_bytes_list[i]) {
      TAKYON_RECORD_ERROR(path->attrs.error_message,
                          "Both endpoints, sharing B to A buffer, are using a different values for recver_max_bytes_list[%d]=%lld and sender_max_bytes_list[%d]=%lld\n",
                          i, (unsigned long long)path->attrs.recver_max_bytes_list[i],
                          i, (unsigned long long)remote_path->attrs.sender_max_bytes_list[i]);
      goto cleanup;
    }
  }

  // Endpoint A sets up memory buffer allocations, and shares with endpoint B
  pthread_mutex_lock(&item->mutex);
  if (path->attrs.is_endpointA) {
    TakyonPrivatePath *remote_private_path = (TakyonPrivatePath *)remote_path->private_path;
    PathBuffers *remote_buffers = (PathBuffers *)remote_private_path->private_data;

    // A to B allocations
    for (int buf_index=0; buf_index<path->attrs.nbufs_AtoB; buf_index++) {
      SingleBuffer *this_send_buffer = &buffers->send_buffer_list[buf_index];
      SingleBuffer *remote_recv_buffer = &remote_buffers->recv_buffer_list[buf_index];

      // Make sure both sides agree on number of buffer bytes
      if (this_send_buffer->sender_max_bytes != remote_recv_buffer->recver_max_bytes) {
        interThreadManagerMarkConnectionAsBad(item);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "buffer_index %d endpoint A to endpoint B bytes is not the same on both sides: %lld, %lld\n", buf_index, (unsigned long long)this_send_buffer->sender_max_bytes, (unsigned long long)remote_recv_buffer->recver_max_bytes);
        pthread_mutex_unlock(&item->mutex);
        goto cleanup;
      }

      // Endpoint A to endpoint B memory
      if (this_send_buffer->sender_max_bytes != 0) {
        // Memory size is non zero, so need to define an address
        if ((this_send_buffer->sender_addr != 0) && (remote_recv_buffer->recver_addr != 0)) {
          // BAD: Both sides already have memory passed in
          interThreadManagerMarkConnectionAsBad(item);
          TAKYON_RECORD_ERROR(path->attrs.error_message, "buffer index %d endpoint A to endpoint B allocations can't be done on both sides since this is a shared pointer path\n", buf_index);
          pthread_mutex_unlock(&item->mutex);
          goto cleanup;
        }
        if (this_send_buffer->sender_addr != 0) {
          // Sender has pre-defined memory buffer, so copy it to the remote recver
          remote_recv_buffer->recver_addr = this_send_buffer->sender_addr;
        } else if (remote_recv_buffer->recver_addr != 0) {
          // Remote receiver has pre-defined memory buffer, so copy it to this sender
          this_send_buffer->sender_addr = remote_recv_buffer->recver_addr;
        } else {
          // Need to allocate buffer, and have the sender manage it
          int alignment = memoryPageSize();
          void *addr;
          uint64_t bytes = path->attrs.sender_max_bytes_list[buf_index];
          if (!memoryAlloc(alignment, bytes, &addr, path->attrs.error_message)) {
            interThreadManagerMarkConnectionAsBad(item);
            TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
            pthread_mutex_unlock(&item->mutex);
            goto cleanup;
          }
          this_send_buffer->addr_alloced = true;
          remote_recv_buffer->recver_addr = (size_t)addr;
          this_send_buffer->sender_addr = (size_t)addr;
        }
      }
    }

    // B to A allocations (still being handled on side A)
    for (int buf_index=0; buf_index<path->attrs.nbufs_BtoA; buf_index++) {
      SingleBuffer *this_recv_buffer = &buffers->recv_buffer_list[buf_index];
      SingleBuffer *remote_send_buffer = &remote_buffers->send_buffer_list[buf_index];

      // Make sure both sides agree on number of buffer bytes
      if (this_recv_buffer->recver_max_bytes != remote_send_buffer->sender_max_bytes) {
        interThreadManagerMarkConnectionAsBad(item);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "buffer_index %d endpoint B to endpoint A bytes is not the same on both sides: %lld, %lld\n", buf_index, (unsigned long long)this_recv_buffer->recver_max_bytes, (unsigned long long)remote_send_buffer->sender_max_bytes);
        pthread_mutex_unlock(&item->mutex);
        goto cleanup;
      }

      // Endpoint B to endpoint A memory
      if (this_recv_buffer->recver_max_bytes != 0) {
        // Memory size is non zero, so need to define an address
        if ((this_recv_buffer->recver_addr != 0) && (remote_send_buffer->sender_addr != 0)) {
          // BAD: Both sides already have memory passed in
          interThreadManagerMarkConnectionAsBad(item);
          TAKYON_RECORD_ERROR(path->attrs.error_message, "buffer index %d endpoint B to endpoint A allocations can't be done on both sides since this is a shared pointer path\n", buf_index);
          pthread_mutex_unlock(&item->mutex);
          goto cleanup;
        }
        if (this_recv_buffer->recver_addr != 0) {
          // Remote sender has pre-defined memory buffer, so copy it to the remote sender
          remote_send_buffer->sender_addr = this_recv_buffer->recver_addr;
        } else if (remote_send_buffer->sender_addr != 0) {
          // Local receiver has pre-defined memory buffer, so copy it to this recver
          this_recv_buffer->recver_addr = remote_send_buffer->sender_addr;
        } else {
          // Need to allocate buffer
          int alignment = memoryPageSize();
          void *addr;
          uint64_t bytes = path->attrs.recver_max_bytes_list[buf_index];
          if (!memoryAlloc(alignment, bytes, &addr, path->attrs.error_message)) {
            interThreadManagerMarkConnectionAsBad(item);
            TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
            pthread_mutex_unlock(&item->mutex);
            goto cleanup;
          }
          this_recv_buffer->addr_alloced = true;
          remote_send_buffer->sender_addr = (size_t)addr;
          this_recv_buffer->recver_addr = (size_t)addr;
        }
      }
    }

    // Let endpoint B know the connection is made
    item->memory_buffers_syned = true;
    pthread_cond_signal(&item->cond);

  } else {
    // Endpoint B: wait for endpoint A to set shared addresses
    while (!item->memory_buffers_syned) {
      bool timed_out;
      bool suceeded = threadCondWait(&item->mutex, &item->cond, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
      if ((!suceeded) || (timed_out)) {
        interThreadManagerMarkConnectionAsBad(item);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed get shared addresses\n");
        pthread_mutex_unlock(&item->mutex);
        goto cleanup;
      }
    }
  }

  // Can now unlock
  pthread_mutex_unlock(&item->mutex);

  // Need to get the addresses back to the application on both endpoints of the path
  for (int buf_index=0; buf_index<path->attrs.nbufs_AtoB; buf_index++) {
    SingleBuffer *this_buffer = &buffers->send_buffer_list[buf_index];
    if (this_buffer->sender_max_bytes != 0) {
      if (path->attrs.sender_addr_list[buf_index] == 0) {
        path->attrs.sender_addr_list[buf_index] = this_buffer->sender_addr;
      }
    }
  }
  for (int buf_index=0; buf_index<path->attrs.nbufs_BtoA; buf_index++) {
    SingleBuffer *this_buffer = &buffers->recv_buffer_list[buf_index];
    if (this_buffer->recver_max_bytes != 0) {
      if (path->attrs.recver_addr_list[buf_index] == 0) {
        path->attrs.recver_addr_list[buf_index] = this_buffer->recver_addr;
      }
    }
  }

  // Ready to start transferring
  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  freePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setInterThreadPointerFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
