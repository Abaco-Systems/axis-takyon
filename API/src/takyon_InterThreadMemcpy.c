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
//   This is a Takyon interface to the inter-thread memcpy() interface.
//   memcpy() is used to do transfers between two threads in the same process.
//   Posix mutex and conditional variables are used for the synchronization.
//   Each endpoint has it own memory buffers
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define THREAD_MANAGER_ID 23 // This must be different from the other interconnects that use the thread manager

typedef struct {
  uint64_t sender_max_bytes;
  uint64_t recver_max_bytes;
  bool sender_addr_alloced;
  bool recver_addr_alloced;
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
  InterThreadManagerItem *shared_thread_item = buffers->shared_thread_item;

  // Lock the mutex now since many of the variables come from the remote side
  pthread_mutex_lock(&shared_thread_item->mutex);

  // Verify connection is good
  if (shared_thread_item->connection_broken) {
    pthread_mutex_unlock(&shared_thread_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Check if waiting on a takyonIsSendFinished()
  if (buffer->send_started) {
    interThreadManagerMarkConnectionAsBad(shared_thread_item);
    pthread_mutex_unlock(&shared_thread_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonIsSendFinished() was not called\n", buffer_index);
    return false;
  }

  // Validate fits on recver
  TakyonPath *remote_path = path->attrs.is_endpointA ? shared_thread_item->pathB : shared_thread_item->pathA;
  uint64_t max_recver_bytes = remote_path->attrs.recver_max_bytes_list[buffer_index];
  uint64_t total_bytes_to_recv = dest_offset + bytes;
  if (total_bytes_to_recv > max_recver_bytes) {
    interThreadManagerMarkConnectionAsBad(shared_thread_item);
    pthread_mutex_unlock(&shared_thread_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of range for the recver. Exceeding by %lld bytes\n", total_bytes_to_recv - max_recver_bytes);
    return false;
  }

  // Transfer the data
  TakyonPrivatePath *remote_private_path = (TakyonPrivatePath *)remote_path->private_path;
  PathBuffers *remote_buffers = (PathBuffers *)remote_private_path->private_data;
  SingleBuffer *remote_buffer = &remote_buffers->recv_buffer_list[buffer_index];
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
  void *remote_addr = (void *)(remote_buffer->recver_addr + dest_offset);
  memcpy(remote_addr, sender_addr, bytes);
  buffer->send_started = true;

  // Fill in the remote recv info
  remote_buffer->bytes_recved = bytes;
  remote_buffer->offset_recved = dest_offset;
  remote_buffer->got_data = true;
  if (!path->attrs.is_polling) {
    // Signal receiver
    pthread_cond_signal(&shared_thread_item->cond);
  }

  // Done with remote variables
  pthread_mutex_unlock(&shared_thread_item->mutex);

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
  InterThreadManagerItem *shared_thread_item = buffers->shared_thread_item;

  // Lock the mutex now since many of the variables come from the remote side
  pthread_mutex_lock(&shared_thread_item->mutex);

  // Verify connection is good
  if (shared_thread_item->connection_broken) {
    pthread_mutex_unlock(&shared_thread_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Verify no double writes
  if (!buffer->send_started) {
    interThreadManagerMarkConnectionAsBad(shared_thread_item);
    pthread_mutex_unlock(&shared_thread_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "takyonIsSendFinished() was called, but a prior takyonSend() was not called on buffer %d\n", buffer_index);
    return false;
  }

  // Since memcpy can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;

  pthread_mutex_unlock(&shared_thread_item->mutex);

  return true;
}

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  PathBuffers *buffers = (PathBuffers *)private_path->private_data;
  SingleBuffer *buffer = &buffers->recv_buffer_list[buffer_index];
  int64_t time1 = 0;
  InterThreadManagerItem *shared_thread_item = buffers->shared_thread_item;

  if (path->attrs.is_polling) {
    time1 = clockTimeNanoseconds();
    // IMPORTANT: The mutex is unlocked while spinning on 'waiting for data' or 'the connection is broken',
    //            but should be fine since both are single intergers and mutually exclusive
  } else {
    // Lock the mutex now since many of the variables come from the remote side
    pthread_mutex_lock(&shared_thread_item->mutex);
  }

  // Verbosity
  if (!buffer->got_data && (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV_MORE)) {
    printf("%-15s (%s:%s) Waiting for data on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // See if the data has been sent
  while (!buffer->got_data && !shared_thread_item->connection_broken) {
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
      bool suceeded = threadCondWait(&shared_thread_item->mutex, &shared_thread_item->cond, private_path->recv_start_timeout_ns, &timed_out, path->attrs.error_message);
      if (!suceeded) {
        interThreadManagerMarkConnectionAsBad(shared_thread_item);
        pthread_mutex_unlock(&shared_thread_item->mutex);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to wait for data\n");
        return false;
      }
      if (timed_out) {
        pthread_mutex_unlock(&shared_thread_item->mutex);
        if (timed_out_ret != NULL) *timed_out_ret = true;
        return true;
      }
    }
  }

  // Verify connection is good
  if (shared_thread_item->connection_broken) {
    if (!path->attrs.is_polling) pthread_mutex_unlock(&shared_thread_item->mutex);
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
  if (!path->attrs.is_polling) pthread_mutex_unlock(&shared_thread_item->mutex);

  return true;
}

// Shared by tknCreate() and tknDestroy() functions
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
  //   "InterThreadMemcpy -ID=<ID>"
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

  // Allocate private handle
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
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
    buffers->send_buffer_list[buf_index].sender_max_bytes = sender_bytes;
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Recver
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr == 0) {
        buffers->recv_buffer_list[buf_index].recver_addr_alloced = true;
      } else {
        buffers->recv_buffer_list[buf_index].recver_addr = recver_addr;
      }
    }
    buffers->recv_buffer_list[buf_index].recver_max_bytes = recver_bytes;
  }

  // Create local sender memory
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    uint64_t sender_bytes = path->attrs.sender_max_bytes_list[buf_index];
    if (sender_bytes > 0) {
      if (path->attrs.sender_addr_list[buf_index] == 0) {
        int alignment = memoryPageSize();
        void *addr;
        if (!memoryAlloc(alignment, sender_bytes, &addr, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
          goto cleanup;
        }
        path->attrs.sender_addr_list[buf_index] = (size_t)addr;
        buffers->send_buffer_list[buf_index].sender_addr = (size_t)addr;
      } else {
        // Was already provided by the application
      }
    }
  }

  // Create local recver memory
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      if (path->attrs.recver_addr_list[buf_index] == 0) {
        int alignment = memoryPageSize();
        void *addr;
        if (!memoryAlloc(alignment, recver_bytes, &addr, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
          goto cleanup;
        }
        path->attrs.recver_addr_list[buf_index] = (size_t)addr;
        buffers->recv_buffer_list[buf_index].recver_addr = (size_t)addr;
      } else {
        // Was already provided by the application
      }
    }
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

  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  freePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
void setInterThreadMemcpyFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
