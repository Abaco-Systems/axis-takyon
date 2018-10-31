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
//   This is a Takyon interface to the inter-thread memcpy() interface.
//   memcpy() is used to do the transfers.
//   Posix mutex and conditional variables are used for the synchronization.
//   This interconnect also supports shared memory where the source and
//   destination share the same buffers.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

typedef struct {
  uint64_t sender_max_bytes;
  uint64_t recver_max_bytes;
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
  bool send_started;
  volatile bool got_data;
  uint64_t num_blocks_recved;
  uint64_t bytes_per_block_recved;
  uint64_t offset_recved;
  uint64_t stride_recved;
} MemcpyXferBuffer;

typedef struct {
  uint64_t sender_max_bytes;
  uint64_t recver_max_bytes;
  bool addr_alloced;
  size_t sender_addr;
  size_t recver_addr;
  bool send_started;
  volatile bool got_data;
  uint64_t num_blocks_recved;
  uint64_t bytes_per_block_recved;
  uint64_t offset_recved;
  uint64_t stride_recved;
} PointerXferBuffer;

typedef struct _MemcpyPath MemcpyPath;

struct _MemcpyPath {
  bool is_shared_pointer;
  ThreadManagerItem *shared_thread_item;
  MemcpyXferBuffer *memcpy_send_buffer_list;
  MemcpyXferBuffer *memcpy_recv_buffer_list;
  PointerXferBuffer *pointer_send_buffer_list;
  PointerXferBuffer *pointer_recv_buffer_list;
};

static bool memcpySend(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  MemcpyXferBuffer *buffer = &memcpy_path->memcpy_send_buffer_list[buffer_index];
  ThreadManagerItem *shared_item = memcpy_path->shared_thread_item;

  // Lock the mutex now since many of the variables come from the remote side
  pthread_mutex_lock(&shared_item->mutex);

  // Verify connection is good
  if (shared_item->connection_broken) {
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Check if waiting on a sendTest()
  if (buffer->send_started) {
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonSendTest() was not called\n", buffer_index);
    return false;
  }

  // Validate fits on recver
  TakyonPath *remote_path = path->attrs.is_endpointA ? shared_item->pathB : shared_item->pathA;
  uint64_t max_recver_bytes = remote_path->attrs.recver_max_bytes_list[buffer_index];
  uint64_t total_bytes_to_recv = dest_offset + num_blocks*bytes_per_block + (num_blocks-1)*dest_stride;
  if (total_bytes_to_recv > max_recver_bytes) {
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of range for the recver. Exceeding by %lld bytes\n", total_bytes_to_recv - max_recver_bytes);
    return false;
  }

  // Transfer the data
  TakyonPrivatePath *remote_private_path = (TakyonPrivatePath *)remote_path->private;
  MemcpyPath *remote_memcpy_path = (MemcpyPath *)remote_private_path->private;
  MemcpyXferBuffer *remote_buffer = &remote_memcpy_path->memcpy_recv_buffer_list[buffer_index];
  void *sender_addr = (void *)(buffer->sender_addr + src_offset);
  void *remote_addr = (void *)(remote_buffer->recver_addr + dest_offset);
  for (int i=0; i<num_blocks; i++) {
    memcpy(remote_addr, sender_addr, bytes_per_block);
    sender_addr = (void *)((uint64_t)sender_addr + src_stride);
    remote_addr = (void *)((uint64_t)remote_addr + dest_stride);
  }
  buffer->send_started = true;

  // Fill in the remote recv info
  remote_buffer->num_blocks_recved = num_blocks;
  remote_buffer->bytes_per_block_recved = bytes_per_block;
  remote_buffer->offset_recved = dest_offset;
  remote_buffer->stride_recved = dest_stride;
  remote_buffer->got_data = true;
  if (!path->attrs.is_polling) {
    // Signal receiver
    pthread_cond_signal(&shared_item->cond);
  }

  // Done with remote variables
  pthread_mutex_unlock(&shared_item->mutex);

  // Handle completion
  if (path->attrs.send_completion_method == TAKYON_BLOCKING) {
    buffer->send_started = false;
  } else if (path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    // Nothing to do
  }

  return true;
}

static bool pointerSend(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *pointer_path = (MemcpyPath *)private_path->private;
  PointerXferBuffer *buffer = &pointer_path->pointer_send_buffer_list[buffer_index];
  ThreadManagerItem *shared_item = pointer_path->shared_thread_item;

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
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonSendTest() was not called\n", buffer_index);
    return false;
  }
  if (src_offset != dest_offset) {
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The source offset=%lld and destination offset=%lld are not the same.\n", (unsigned long long)src_offset, (unsigned long long)dest_offset);
    return false;
  }
  if (src_stride != dest_stride) {
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The source stride=%lld and destination stride=%lld are not the same.\n", (unsigned long long)src_stride, (unsigned long long)dest_stride);
    return false;
  }

  // Transfer the data
  buffer->send_started = true;

  // Set some sync flags, and signal the receiver
  TakyonPath *remote_path = path->attrs.is_endpointA ? shared_item->pathB : shared_item->pathA;
  TakyonPrivatePath *remote_private_path = (TakyonPrivatePath *)remote_path->private;
  MemcpyPath *remote_pointer_path = (MemcpyPath *)remote_private_path->private;
  PointerXferBuffer *remote_buffer = &remote_pointer_path->pointer_recv_buffer_list[buffer_index];
  remote_buffer->num_blocks_recved = num_blocks;
  remote_buffer->bytes_per_block_recved = bytes_per_block;
  remote_buffer->offset_recved = dest_offset;
  remote_buffer->stride_recved = dest_stride;
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
  } else if (path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    // Nothing to do
  }

  return true;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  if (memcpy_path->is_shared_pointer) {
    return pointerSend(path, buffer_index, num_blocks, bytes_per_block, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
  } else {
    return memcpySend(path, buffer_index, num_blocks, bytes_per_block, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
  }
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  uint64_t num_blocks = 1;
  uint64_t src_stride = 0;
  uint64_t dest_stride = 0;
  if (memcpy_path->is_shared_pointer) {
    return pointerSend(path, buffer_index, num_blocks, bytes, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
  } else {
    return memcpySend(path, buffer_index, num_blocks, bytes, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
  }
}

static bool memcpySendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  MemcpyXferBuffer *buffer = &memcpy_path->memcpy_send_buffer_list[buffer_index];
  ThreadManagerItem *shared_item = memcpy_path->shared_thread_item;

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
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "sendTest() was called, but a prior send() was not called on buffer %d\n", buffer_index);
    return false;
  }

  // Since memcpy can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;

  pthread_mutex_unlock(&shared_item->mutex);

  return true;
}

static bool pointerSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *pointer_path = (MemcpyPath *)private_path->private;
  PointerXferBuffer *buffer = &pointer_path->pointer_send_buffer_list[buffer_index];
  ThreadManagerItem *shared_item = pointer_path->shared_thread_item;

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
    threadManagerMarkConnectionAsBad(shared_item);
    pthread_mutex_unlock(&shared_item->mutex);
    TAKYON_RECORD_ERROR(path->attrs.error_message, "sendTest() was called, but a prior send() was not called on buffer %d\n", buffer_index);
    return false;
  }

  // Since this interconnect can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;

  pthread_mutex_unlock(&shared_item->mutex);

  return true;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  if (memcpy_path->is_shared_pointer) {
    return pointerSendTest(path, buffer_index, timed_out_ret);
  } else {
    return memcpySendTest(path, buffer_index, timed_out_ret);
  }
}

static bool memcpyRecv(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  MemcpyXferBuffer *buffer = &memcpy_path->memcpy_recv_buffer_list[buffer_index];
  int64_t time1 = 0;
  ThreadManagerItem *shared_item = memcpy_path->shared_thread_item;

  if (path->attrs.is_polling) {
    time1 = clockTimeNanoseconds();
    // IMPORTANT: The mutex is unlocked while spinning waiting for data or the connection is broke, but this is no longer atomic,
    //            but should be fine since both are single intergers and mutually exclusive
  } else {
    // Lock the mutex now since many of the variables come from the remote side
    pthread_mutex_lock(&shared_item->mutex);
  }

  // Verbosity
  if (!buffer->got_data && (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS)) {
    printf("%-15s (%s:%s) Waiting for data on buffer %d\n",
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
      if (private_path->recv_complete_timeout_ns == TAKYON_NO_WAIT) {
        // No timeout, so return now
        *timed_out_ret = true;
        return true;
      } else if (private_path->recv_complete_timeout_ns >= 0) {
        // Hit the timeout without data, time to return
        int64_t time2 = clockTimeNanoseconds();
        int64_t diff = time2 - time1;
        if (diff > private_path->recv_complete_timeout_ns) {
          *timed_out_ret = true;
          return true;
        }
      }
    } else {
      // Sleep while waiting for data
      bool timed_out;
      bool suceeded = threadCondWait(&shared_item->mutex, &shared_item->cond, private_path->recv_complete_timeout_ns, &timed_out, path->attrs.error_message);
      if (!suceeded) {
        threadManagerMarkConnectionAsBad(shared_item);
        pthread_mutex_unlock(&shared_item->mutex);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to wait for data\n");
        return false;
      }
      if (timed_out) {
        pthread_mutex_unlock(&shared_item->mutex);
        *timed_out_ret = true;
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
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
    printf("%-15s (%s:%s) Received %lld blocks, with %lld bytes per block, at offset %lld, and stride %lld, on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           (unsigned long long)buffer->num_blocks_recved,
           (unsigned long long)buffer->bytes_per_block_recved,
           (unsigned long long)buffer->offset_recved,
           (unsigned long long)buffer->stride_recved,
           buffer_index);
  }

  if (num_blocks_ret != NULL) *num_blocks_ret = buffer->num_blocks_recved;
  if (bytes_per_block_ret != NULL) *bytes_per_block_ret = buffer->bytes_per_block_recved;
  if (offset_ret != NULL) *offset_ret = buffer->offset_recved;
  if (stride_ret != NULL) *stride_ret = buffer->stride_recved;
  buffer->got_data = false;
  buffer->num_blocks_recved = 0;
  buffer->bytes_per_block_recved = 0;
  buffer->offset_recved = 0;
  buffer->stride_recved = 0;

  // Unlock
  if (!path->attrs.is_polling) pthread_mutex_unlock(&shared_item->mutex);

  return true;
}

static bool pointerRecv(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *pointer_path = (MemcpyPath *)private_path->private;
  PointerXferBuffer *buffer = &pointer_path->pointer_recv_buffer_list[buffer_index];
  int64_t time1 = 0;
  ThreadManagerItem *shared_item = pointer_path->shared_thread_item;

  if (path->attrs.is_polling) {
    time1 = clockTimeNanoseconds();
    // IMPORTANT: The mutex is unlocked while spinning waiting for data or the connection is broke, but this is no longer atomic,
    //            but should be fine since both are single intergers and mutually exclusive
  } else {
    // Lock the mutex now since many of the variables come from the remote side
    pthread_mutex_lock(&shared_item->mutex);
  }

  // Verbosity
  if (!buffer->got_data && (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS)) {
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
      if (private_path->recv_complete_timeout_ns == TAKYON_NO_WAIT) {
        // No timeout, so return now
        *timed_out_ret = true;
        return true;
      } else if (private_path->recv_complete_timeout_ns >= 0) {
        // Hit the timeout without data, time to return
        int64_t time2 = clockTimeNanoseconds();
        int64_t diff = time2 - time1;
        if (diff > private_path->recv_complete_timeout_ns) {
          *timed_out_ret = true;
          return true;
        }
      }
    } else {
      // Sleep while waiting for data
      bool timed_out;
      bool suceeded = threadCondWait(&shared_item->mutex, &shared_item->cond, private_path->recv_complete_timeout_ns, &timed_out, path->attrs.error_message);
      if (!suceeded) {
        threadManagerMarkConnectionAsBad(shared_item);
        pthread_mutex_unlock(&shared_item->mutex);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to wait for data\n");
        return false;
      }
      if (timed_out) {
        pthread_mutex_unlock(&shared_item->mutex);
        *timed_out_ret = true;
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
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
    printf("%-15s (%s:%s) Received %lld blocks, with %lld bytes per block, at offset %lld, and stride %lld, on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           (unsigned long long)buffer->num_blocks_recved,
           (unsigned long long)buffer->bytes_per_block_recved,
           (unsigned long long)buffer->offset_recved,
           (unsigned long long)buffer->stride_recved,
           buffer_index);
  }

  if (num_blocks_ret != NULL) *num_blocks_ret = buffer->num_blocks_recved;
  if (bytes_per_block_ret != NULL) *bytes_per_block_ret = buffer->bytes_per_block_recved;
  if (offset_ret != NULL) *offset_ret = buffer->offset_recved;
  if (stride_ret != NULL) *stride_ret = buffer->stride_recved;
  buffer->got_data = false;
  buffer->num_blocks_recved = 0;
  buffer->bytes_per_block_recved = 0;
  buffer->offset_recved = 0;
  buffer->stride_recved = 0;

  // Unlock
  if (!path->attrs.is_polling) pthread_mutex_unlock(&shared_item->mutex);

  return true;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  if (memcpy_path->is_shared_pointer) {
    return pointerRecv(path, buffer_index, num_blocks_ret, bytes_per_block_ret, offset_ret, stride_ret, timed_out_ret);
  } else {
    return memcpyRecv(path, buffer_index, num_blocks_ret, bytes_per_block_ret, offset_ret, stride_ret, timed_out_ret);
  }
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  uint64_t num_blocks;
  uint64_t stride;
  bool timed_out = false;
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  ThreadManagerItem *shared_item = memcpy_path->shared_thread_item;
  bool ok;

  if (memcpy_path->is_shared_pointer) {
    ok = pointerRecv(path, buffer_index, &num_blocks, bytes_ret, offset_ret, &stride, &timed_out);
  } else {
    ok = memcpyRecv(path, buffer_index, &num_blocks, bytes_ret, offset_ret, &stride, &timed_out);
  }

  if (!ok) return false;
  if (!timed_out) {
    // Verify it was not a strided transfer, else complain the wrong API was used
    if ((num_blocks > 1) || (stride > 0)) {
      pthread_mutex_lock(&shared_item->mutex);
      threadManagerMarkConnectionAsBad(shared_item);
      pthread_mutex_unlock(&shared_item->mutex);
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received strided data using takyonRecv() but requires takyonRecvStrided(): num_blocks=%lld, stride=%lld\n", (unsigned long long)num_blocks, (unsigned long long)stride);
      return false;
    }
  }
  if (timed_out_ret != NULL) *timed_out_ret = timed_out;
  return true;
}

static void memcpyFreePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;

  // Free buffer resources
  if (memcpy_path->memcpy_send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the send buffers, if path managed
      if (memcpy_path->memcpy_send_buffer_list[buf_index].sender_addr_alloced && (memcpy_path->memcpy_send_buffer_list[buf_index].sender_addr != 0)) {
        memoryFree((void *)memcpy_path->memcpy_send_buffer_list[buf_index].sender_addr, path->attrs.error_message);
      }
    }
    free(memcpy_path->memcpy_send_buffer_list);
  }
  if (memcpy_path->memcpy_recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      // Free the recv buffers, if path managed
      if (memcpy_path->memcpy_recv_buffer_list[buf_index].recver_addr_alloced && (memcpy_path->memcpy_recv_buffer_list[buf_index].recver_addr != 0)) {
        memoryFree((void *)memcpy_path->memcpy_recv_buffer_list[buf_index].recver_addr, path->attrs.error_message);
      }
    }
    free(memcpy_path->memcpy_recv_buffer_list);
  }

  // Free the private handle
  free(memcpy_path);
}

static void pointerFreePathMemoryResources(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *pointer_path = (MemcpyPath *)private_path->private;

  // Free buffer resources (managed by side A)
  if (path->attrs.is_endpointA) {
    // A to B allocations
    for (int buf_index=0; buf_index<path->attrs.nbufs_AtoB; buf_index++) {
      PointerXferBuffer *this_send_buffer = &pointer_path->pointer_send_buffer_list[buf_index];
      if (this_send_buffer->addr_alloced && (this_send_buffer->sender_addr != 0)) {
        memoryFree((void *)this_send_buffer->sender_addr, path->attrs.error_message);
      }
    }

    // B to A allocations
    for (int buf_index=0; buf_index<path->attrs.nbufs_BtoA; buf_index++) {
      PointerXferBuffer *this_recv_buffer = &pointer_path->pointer_recv_buffer_list[buf_index];
      if (this_recv_buffer->addr_alloced && (this_recv_buffer->recver_addr != 0)) {
        memoryFree((void *)this_recv_buffer->recver_addr, path->attrs.error_message);
      }
    }
  }

  free(pointer_path->pointer_send_buffer_list);
  free(pointer_path->pointer_recv_buffer_list);

  // Free the private handle
  free(pointer_path);
}

static bool memcpyDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  // Do a coordinated disconnect
  bool graceful_disconnect_ok = true;
  if (!threadManagerDisconnect(path, memcpy_path->shared_thread_item)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Thread disconnect failed\n");
    graceful_disconnect_ok = false;
  }

  // Free path memory resources
  memcpyFreePathMemoryResources(path);
  return graceful_disconnect_ok;
}

static bool pointerDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *pointer_path = (MemcpyPath *)private_path->private;

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  // Do a coordinated disconnect
  bool graceful_disconnect_ok = true;
  if (!threadManagerDisconnect(path, pointer_path->shared_thread_item)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Thread disconnect failed\n");
    graceful_disconnect_ok = false;
  }

  // Free path memory resources
  pointerFreePathMemoryResources(path);
  return graceful_disconnect_ok;
}

#ifdef BUILD_STATIC_LIB
static
#endif
bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MemcpyPath *memcpy_path = (MemcpyPath *)private_path->private;
  if (memcpy_path->is_shared_pointer) {
    return pointerDestroy(path);
  } else {
    return memcpyDestroy(path);
  }
}

static bool verifyConsistentEndpointAttributeValues(TakyonPath *path, ThreadManagerItem *item, bool is_shared_pointer) {
  TakyonPath *remote_path = path->attrs.is_endpointA ? item->pathB : item->pathA;
  if (path->attrs.is_polling != remote_path->attrs.is_polling) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->is_polling\n");
    return false;
  }
  if (path->attrs.nbufs_AtoB != remote_path->attrs.nbufs_AtoB) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_AtoB: (%d, %d)\n", path->attrs.nbufs_AtoB, remote_path->attrs.nbufs_AtoB);
    return false;
  }
  if (path->attrs.nbufs_BtoA != remote_path->attrs.nbufs_BtoA) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_BtoA: (%d, %d)\n", path->attrs.nbufs_BtoA, remote_path->attrs.nbufs_BtoA);
    return false;
  }

  // Make sure each sender and remote recver buffers are the same size, but only if shared memory
  if (is_shared_pointer) {
    for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
      if (path->attrs.sender_max_bytes_list[i] != remote_path->attrs.recver_max_bytes_list[i]) {
        TAKYON_RECORD_ERROR(path->attrs.error_message,
                            "Both endpoints, sharing A to B buffer, are using a different values for sender_max_bytes_list[%d]=%lld and recver_max_bytes_list[%d]=%lld\n",
                            i, (unsigned long long)path->attrs.sender_max_bytes_list[i],
                            i, (unsigned long long)remote_path->attrs.recver_max_bytes_list[i]);
        return false;
      }
    }
    for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
      if (path->attrs.recver_max_bytes_list[i] != remote_path->attrs.sender_max_bytes_list[i]) {
        TAKYON_RECORD_ERROR(path->attrs.error_message,
                            "Both endpoints, sharing B to A buffer, are using a different values for recver_max_bytes_list[%d]=%lld and sender_max_bytes_list[%d]=%lld\n",
                            i, (unsigned long long)path->attrs.recver_max_bytes_list[i],
                            i, (unsigned long long)remote_path->attrs.sender_max_bytes_list[i]);
        return false;
      }
    }
  }
  return true;
}

static bool memcpyCreate(TakyonPath *path, int path_id) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  // Allocate private handle
  MemcpyPath *memcpy_path = calloc(1, sizeof(MemcpyPath));
  if (memcpy_path == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  memcpy_path->is_shared_pointer = false;
  private_path->private = memcpy_path;

  // Allocate the buffers list
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  memcpy_path->memcpy_send_buffer_list = calloc(nbufs_sender, sizeof(MemcpyXferBuffer));
  if (memcpy_path->memcpy_send_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }
  memcpy_path->memcpy_recv_buffer_list = calloc(nbufs_recver, sizeof(MemcpyXferBuffer));
  if (memcpy_path->memcpy_recv_buffer_list == NULL) {
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
        memcpy_path->memcpy_send_buffer_list[buf_index].sender_addr_alloced = true;
      } else {
        memcpy_path->memcpy_send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
    memcpy_path->memcpy_send_buffer_list[buf_index].sender_max_bytes = sender_bytes;
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Recver
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr == 0) {
        memcpy_path->memcpy_recv_buffer_list[buf_index].recver_addr_alloced = true;
      } else {
        memcpy_path->memcpy_recv_buffer_list[buf_index].recver_addr = recver_addr;
      }
    }
    memcpy_path->memcpy_recv_buffer_list[buf_index].recver_max_bytes = recver_bytes;
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
        memcpy_path->memcpy_send_buffer_list[buf_index].sender_addr = (size_t)addr;
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
        memcpy_path->memcpy_recv_buffer_list[buf_index].recver_addr = (size_t)addr;
      } else {
        // Was already provided by the application
      }
    }
  }

  // Connect to the remote thread
  ThreadManagerItem *item = threadManagerConnect(MEMCPY_INTERCONNECT_ID, path_id, path);
  if (item == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to connect to remote thread\n");
    goto cleanup;
  }
  memcpy_path->shared_thread_item = item;

  // Verify endpoints have the same attributes
  if (!verifyConsistentEndpointAttributeValues(path, item, false)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Inconsistent endpoint attribute values\n");
    goto cleanup;
  }

  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  memcpyFreePathMemoryResources(path);
  return false;
}

static bool pointerCreate(TakyonPath *path, int path_id) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  // Allocate private handle
  MemcpyPath *pointer_path = calloc(1, sizeof(MemcpyPath));
  if (pointer_path == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  pointer_path->is_shared_pointer = true;
  private_path->private = pointer_path;

  // Allocate the buffers list
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  pointer_path->pointer_send_buffer_list = calloc(nbufs_sender, sizeof(PointerXferBuffer));
  if (pointer_path->pointer_send_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }
  pointer_path->pointer_recv_buffer_list = calloc(nbufs_recver, sizeof(PointerXferBuffer));
  if (pointer_path->pointer_recv_buffer_list == NULL) {
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
        pointer_path->pointer_send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
    pointer_path->pointer_send_buffer_list[buf_index].sender_max_bytes = sender_bytes;
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Recver
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    if (recver_bytes > 0) {
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      if (recver_addr != 0) {
        pointer_path->pointer_recv_buffer_list[buf_index].recver_addr = recver_addr;
      }
    }
    pointer_path->pointer_recv_buffer_list[buf_index].recver_max_bytes = recver_bytes;
  }

  // Connect to the remote thread
  ThreadManagerItem *item = threadManagerConnect(POINTER_INTERCONNECT_ID, path_id, path);
  if (item == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to connect to remote thread\n");
    goto cleanup;
  }
  pointer_path->shared_thread_item = item;

  // Verify endpoints have the same attributes
  if (!verifyConsistentEndpointAttributeValues(path, item, true)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Inconsistent endpoint attribute values\n");
    goto cleanup;
  }

  // Endpoint A sets up memory buffer allocations, and shares with endpoint B
  pthread_mutex_lock(&item->mutex);
  if (path->attrs.is_endpointA) {
    TakyonPath *remote_path = path->attrs.is_endpointA ? item->pathB : item->pathA;
    TakyonPrivatePath *remote_private_path = (TakyonPrivatePath *)remote_path->private;
    MemcpyPath *remote_pointer_path = (MemcpyPath *)remote_private_path->private;

    // A to B allocations
    for (int buf_index=0; buf_index<path->attrs.nbufs_AtoB; buf_index++) {
      PointerXferBuffer *this_send_buffer = &pointer_path->pointer_send_buffer_list[buf_index];
      PointerXferBuffer *remote_recv_buffer = &remote_pointer_path->pointer_recv_buffer_list[buf_index];

      // Make sure both sides agree on number of buffer bytes
      if (this_send_buffer->sender_max_bytes != remote_recv_buffer->recver_max_bytes) {
        threadManagerMarkConnectionAsBad(item);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "buffer_index %d endpoint A to endpoint B bytes is not the same on both sides: %lld, %lld\n", buf_index, (unsigned long long)this_send_buffer->sender_max_bytes, (unsigned long long)remote_recv_buffer->recver_max_bytes);
        pthread_mutex_unlock(&item->mutex);
        goto cleanup;
      }

      // Endpoint A to endpoint B memory
      if (this_send_buffer->sender_max_bytes != 0) {
        // Memory size is non zero, so need to define an address
        if ((this_send_buffer->sender_addr != 0) && (remote_recv_buffer->recver_addr != 0)) {
          // BAD: Both sides already have memory passed in
          threadManagerMarkConnectionAsBad(item);
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
            threadManagerMarkConnectionAsBad(item);
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
      PointerXferBuffer *this_recv_buffer = &pointer_path->pointer_recv_buffer_list[buf_index];
      PointerXferBuffer *remote_send_buffer = &remote_pointer_path->pointer_send_buffer_list[buf_index];

      // Make sure both sides agree on number of buffer bytes
      if (this_recv_buffer->recver_max_bytes != remote_send_buffer->sender_max_bytes) {
        threadManagerMarkConnectionAsBad(item);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "buffer_index %d endpoint B to endpoint A bytes is not the same on both sides: %lld, %lld\n", buf_index, (unsigned long long)this_recv_buffer->recver_max_bytes, (unsigned long long)remote_send_buffer->sender_max_bytes);
        pthread_mutex_unlock(&item->mutex);
        goto cleanup;
      }

      // Endpoint B to endpoint A memory
      if (this_recv_buffer->recver_max_bytes != 0) {
        // Memory size is non zero, so need to define an address
        if ((this_recv_buffer->recver_addr != 0) && (remote_send_buffer->sender_addr != 0)) {
          // BAD: Both sides already have memory passed in
          threadManagerMarkConnectionAsBad(item);
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
            threadManagerMarkConnectionAsBad(item);
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
      bool suceeded = threadCondWait(&item->mutex, &item->cond, private_path->create_timeout_ns, &timed_out, path->attrs.error_message);
      if ((!suceeded) || (timed_out)) {
        threadManagerMarkConnectionAsBad(item);
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
    PointerXferBuffer *this_buffer = &pointer_path->pointer_send_buffer_list[buf_index];
    if (this_buffer->sender_max_bytes != 0) {
      if (path->attrs.sender_addr_list[buf_index] == 0) {
        path->attrs.sender_addr_list[buf_index] = this_buffer->sender_addr;
      }
    }
  }
  for (int buf_index=0; buf_index<path->attrs.nbufs_BtoA; buf_index++) {
    PointerXferBuffer *this_buffer = &pointer_path->pointer_recv_buffer_list[buf_index];
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
  pointerFreePathMemoryResources(path);
  return false;
}

#ifdef BUILD_STATIC_LIB
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

  // Call this to make sure the mutex manager is ready to coordinate: This can be called multiple times, but it's garanteed to atomically run only the first time called.
  if (!threadManagerInit()) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to start the mutex manager\n");
    return false;
  }

  // Supported formats:
  //   "Memcpy -ID <ID> [-share]"
  int path_id;
  bool found;
  bool ok = argGetInt(path->attrs.interconnect, "-ID", &path_id, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -ID <value>\n");
    return false;
  }

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) create path (id=%d)\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           path_id);
  }

  // Check if shared pointer or memcpy
  bool is_shared_pointer = argGetFlag(path->attrs.interconnect, "-share");
  if (is_shared_pointer) {
    return pointerCreate(path, path_id);
  } else {
    return memcpyCreate(path, path_id);
  }
}

#ifdef BUILD_STATIC_LIB
void setMemcpyFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknSendStrided = tknSendStrided;
  private_path->tknSendTest = tknSendTest;
  private_path->tknRecv = tknRecv;
  private_path->tknRecvStrided = tknRecvStrided;
  private_path->tknDestroy = tknDestroy;
}
#endif
