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
//   This is a Takyon interface to the inter-process memcpy() interface.
//   Local sockets are used to coordinate the connection and disconnection.
//   Memory maps are used to get access to the remote process'es memory location.
//   memcpy() is used to do the transfers.
//   Process shared posix mutex and conditional variables are used for the
//   synchronization. This is not supported on windows platforms yet.
//   This interconnect also supports shared memory where the source and
//   destination share the same buffers.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define MAX_PIPE_NAME_CHARS 100
#define NUM_SHARED_SYNC_BYTES (5*sizeof(uint64_t))
// Used by the socket based initilize and finalize communication
#define BARRIER_INIT_STEP1 777777
#define BARRIER_INIT_STEP2 888888
#define BARRIER_INIT_STEP3 999999
#define BARRIER_FINALIZE_STEP1 555555
#define BARRIER_FINALIZE_STEP2 666666

typedef struct {
  uint64_t remote_max_recver_bytes; // Only used by the sender
  bool sender_addr_alloced;
  bool recver_addr_alloced;
  void *remote_device_context;
  size_t sender_addr;
  size_t local_recver_addr;
  size_t remote_recver_addr;
  MmapHandle local_mmap_flags;
  MmapHandle local_mmap_app;
  MmapHandle remote_mmap_flags;
  MmapHandle remote_mmap_app;
  bool send_started;
  volatile uint64_t *local_got_data_ref; // Making this uint64_t instead of bool to avoid alignment issues
  uint64_t *local_num_blocks_recved_ref;
  uint64_t *local_bytes_per_block_recved_ref;
  uint64_t *local_offset_recved_ref;
  uint64_t *local_stride_recved_ref;
  uint64_t *remote_got_data_ref;         // Making this uint64_t instead of bool to avoid alignment issues
  uint64_t *remote_num_blocks_recved_ref;
  uint64_t *remote_bytes_per_block_recved_ref;
  uint64_t *remote_offset_recved_ref;
  uint64_t *remote_stride_recved_ref;
} XferBuffer;

typedef struct {
  TakyonSocket socket_fd;
  bool connected;
  pthread_t socket_thread;
  char pipe_name[MAX_PIPE_NAME_CHARS];
  int read_pipe_fd;
  int write_pipe_fd;
  MmapHandle shared_sync_mem;
  pthread_mutex_t *mutex;
  pthread_cond_t *connection_cond;  // Wake up recv and sync create and destroy
  XferBuffer *send_buffer_list;
  XferBuffer *recv_buffer_list;
  bool is_shared_pointer;
} MmapPath;

static bool privateSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = (MmapPath *)private_path->private;
  XferBuffer *buffer = &mmap_path->send_buffer_list[buffer_index];

  // Verify connection is good
  // IMPORTANT: this is not protected in a mutex, but should be fine since the send will not go to sleep waiting in a conditional variable
  if (!mmap_path->connected) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Error checking
  if (mmap_path->is_shared_pointer) {
    if (src_offset != dest_offset) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "The source offset=%lld and destination offset=%lld are not the same.\n", (unsigned long long)src_offset, (unsigned long long)dest_offset);
      return false;
    }
    if (src_stride != dest_stride) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "The source stride=%lld and destination stride=%lld are not the same.\n", (unsigned long long)src_stride, (unsigned long long)dest_stride);
      return false;
    }
  }

  // Validate fits on recver
  if (!mmap_path->is_shared_pointer) {
    uint64_t max_recver_bytes = mmap_path->send_buffer_list[buffer_index].remote_max_recver_bytes;
    uint64_t total_bytes_to_recv = dest_offset + num_blocks*bytes_per_block + (num_blocks-1)*dest_stride;
    if (total_bytes_to_recv > max_recver_bytes) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of range for the recver. Exceeding by %lld bytes\n", total_bytes_to_recv - max_recver_bytes);
      return false;
    }
  }

  // Check if waiting on a sendTest()
  if (buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A previous send on buffer %d was started, but takyonSendTest() was not called\n", buffer_index);
    return false;
  }

  // Transfer the data
  if (!mmap_path->is_shared_pointer) {
    void *sender_addr = (void *)(buffer->sender_addr + src_offset);
    void *remote_addr = (void *)(buffer->remote_recver_addr + dest_offset);
    for (int i=0; i<num_blocks; i++) {
      memcpy(remote_addr, sender_addr, bytes_per_block);
      sender_addr = (void *)((uint64_t)sender_addr + src_stride);
      remote_addr = (void *)((uint64_t)remote_addr + dest_stride);
    }
  }
  buffer->send_started = true;

  // Set some sync flags, and signal the receiver
  if (!path->attrs.is_polling) {
    // Lock mutex
    pthread_mutex_lock(mmap_path->mutex);
  }
  *buffer->remote_num_blocks_recved_ref = num_blocks;
  *buffer->remote_bytes_per_block_recved_ref = bytes_per_block;
  *buffer->remote_offset_recved_ref = dest_offset;
  *buffer->remote_stride_recved_ref = dest_stride;
  *buffer->remote_got_data_ref = 1;
  if (!path->attrs.is_polling) {
    // Unlock mutex and signal receiver
    pthread_mutex_unlock(mmap_path->mutex);
    pthread_cond_signal(mmap_path->connection_cond);
  }

  // Handle completion
  if (path->attrs.send_completion_method == TAKYON_BLOCKING) {
    buffer->send_started = false;
  } else if (path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    // Nothing to do
  }

  return true;
}

#ifdef _WIN32
static
#endif
bool tknSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  return privateSendStrided(path, buffer_index, num_blocks, bytes_per_block, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
}

#ifdef _WIN32
static
#endif
bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  uint64_t num_blocks = 1;
  uint64_t src_stride = 0;
  uint64_t dest_stride = 0;
  return privateSendStrided(path, buffer_index, num_blocks, bytes, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
}

#ifdef _WIN32
static
#endif
bool tknSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = (MmapPath *)private_path->private;
  XferBuffer *buffer = &mmap_path->send_buffer_list[buffer_index];

  // Verify connection is good
  // IMPORTANT: this is not protected in a mutex, but should be fine since the send will not go to sleep waiting in a conditional variable
  if (!mmap_path->connected) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Remote side has failed\n");
    return false;
  }

  // Verify no double writes
  if (!buffer->send_started) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "sendTest() was called, but a prior send() was not called on buffer %d\n", buffer_index);
    return false;
  }

  // Since memcpy can't be non-blocking, the transfer is complete.
  // Mark the transfer as complete.
  buffer->send_started = false;
  return true;
}

static bool privateRecv(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = (MmapPath *)private_path->private;
  XferBuffer *buffer = &mmap_path->recv_buffer_list[buffer_index];
  int64_t time1 = 0;

  if (path->attrs.is_polling) {
    time1 = clockTimeNanoseconds();
  } else {
    // Lock
    pthread_mutex_lock(mmap_path->mutex);
  }

  // See if the data has been sent
  while (!(*buffer->local_got_data_ref)) {
    if (!mmap_path->connected) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "detected disconnect\n");
      if (!path->attrs.is_polling) pthread_mutex_unlock(mmap_path->mutex);
      return false;
    }
    if (path->attrs.is_polling) {
      // Check timeout
      if (private_path->recv_complete_timeout_ns == TAKYON_NO_WAIT) {
        *timed_out_ret = 1;
        return true;
      } else if (private_path->recv_complete_timeout_ns >= 0) {
        int64_t time2 = clockTimeNanoseconds();
        int64_t diff = time2 - time1;
        if (diff > private_path->recv_complete_timeout_ns) {
          *timed_out_ret = 1;
          return true;
        }
      }
    } else {
      // Sleep
      // Vebosity
      if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
        printf("%-15s (%s:%s) waiting for data on buffer %d\n",
               __FUNCTION__,
               path->attrs.is_endpointA ? "A" : "B",
               path->attrs.interconnect,
               buffer_index);
      }
      bool timed_out;
      bool suceeded = threadCondWait(mmap_path->mutex, mmap_path->connection_cond, private_path->recv_complete_timeout_ns, &timed_out, path->attrs.error_message);
      if (!suceeded) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "failed to wait for data\n");
        pthread_mutex_unlock(mmap_path->mutex);
        return false;
      }
      if (timed_out) {
        pthread_mutex_unlock(mmap_path->mutex);
        *timed_out_ret = 1;
        return true;
      }
    }
  }

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME_DETAILS) {
    printf("%-15s (%s:%s) Received %lld blocks, with %lld bytes per block, at offset %lld, and stride %lld, on buffer %d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           (unsigned long long)(*buffer->local_num_blocks_recved_ref),
           (unsigned long long)(*buffer->local_bytes_per_block_recved_ref),
           (unsigned long long)(*buffer->local_offset_recved_ref),
           (unsigned long long)(*buffer->local_stride_recved_ref),
           buffer_index);
  }

  if (num_blocks_ret != NULL) *num_blocks_ret = *buffer->local_num_blocks_recved_ref;
  if (bytes_per_block_ret != NULL) *bytes_per_block_ret = *buffer->local_bytes_per_block_recved_ref;
  if (offset_ret != NULL) *offset_ret = *buffer->local_offset_recved_ref;
  if (stride_ret != NULL) *stride_ret = *buffer->local_stride_recved_ref;
  *buffer->local_got_data_ref = 0;
  *buffer->local_num_blocks_recved_ref = 0;
  *buffer->local_bytes_per_block_recved_ref = 0;
  *buffer->local_offset_recved_ref = 0;
  *buffer->local_stride_recved_ref = 0;

  if (!path->attrs.is_polling) {
    // Unlock
    pthread_mutex_unlock(mmap_path->mutex);
  }

  return true;
}

#ifdef _WIN32
static
#endif
bool tknRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  return privateRecv(path, buffer_index, num_blocks_ret, bytes_per_block_ret, offset_ret, stride_ret, timed_out_ret);
}

#ifdef _WIN32
static
#endif
bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  uint64_t num_blocks;
  uint64_t stride;
  bool timed_out = false;
  bool ok = privateRecv(path, buffer_index, &num_blocks, bytes_ret, offset_ret, &stride, &timed_out);
  if (!ok) return false;
  if (!timed_out) {
    if ((num_blocks > 1) || (stride > 0)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Received strided data using takyonRecv() but requires takyonRecvStrided(): num_blocks=%lld, stride=%lld\n", (unsigned long long)num_blocks, (unsigned long long)stride);
      return false;
    }
  }
  if (timed_out_ret != NULL) *timed_out_ret = timed_out;
  return true;
}

static void *socket_disconnect_handler(void *user_arg) {
  TakyonPath *path = (TakyonPath *)user_arg;
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = (MmapPath *)private_path->private;
  bool got_socket_activity;
  bool ok = socketWaitForDisconnectActivity(mmap_path->socket_fd, mmap_path->read_pipe_fd, &got_socket_activity, path->attrs.error_message);
  if ((!ok) || got_socket_activity) {
    if (!path->attrs.is_polling) pthread_mutex_lock(mmap_path->mutex);
    mmap_path->connected = false;
    if (!path->attrs.is_polling) pthread_mutex_unlock(mmap_path->mutex);
    pthread_cond_signal(mmap_path->connection_cond);
  }
  return NULL;
}

static void free_path_resources(TakyonPath *path, bool disconnect_successful) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = (MmapPath *)private_path->private;

  // Endpoint A allocs the mutex and condition var, and endpoint B gets a handle to it (so both need to release it)
  if (mmap_path->shared_sync_mem != NULL) {
    // Release the mutex and cond var
    if (path->attrs.is_endpointA) {
      if (disconnect_successful) {
        // IMPORTANT: If the connection goes down due to a crash or somthing else, freeing these may cause an unexpected error. This may be a memory leak
        pthread_cond_destroy(mmap_path->connection_cond);
        pthread_mutex_destroy(mmap_path->mutex);
      }
    }
    // Free the shared sync memory buffers
    mmapFree(mmap_path->shared_sync_mem, path->attrs.error_message);
  }

  // Free the buffers (the posix buffers are always allocated on the recieve side)
  if (mmap_path->send_buffer_list != NULL) {
    int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      // Free the handle to the remote destination buffers
      if (mmap_path->send_buffer_list[buf_index].remote_mmap_flags != NULL) {
        mmapFree(mmap_path->send_buffer_list[buf_index].remote_mmap_flags, path->attrs.error_message);
      }
      if (mmap_path->send_buffer_list[buf_index].remote_mmap_app != NULL) {
        mmapFree(mmap_path->send_buffer_list[buf_index].remote_mmap_app, path->attrs.error_message);
      }
      // Free the send buffers, if path managed (the send buffers only exists if not pointer sharing)
      if (!mmap_path->is_shared_pointer) {
        if (mmap_path->send_buffer_list[buf_index].sender_addr_alloced && (mmap_path->send_buffer_list[buf_index].sender_addr != 0)) {
          memoryFree((void *)mmap_path->send_buffer_list[buf_index].sender_addr, path->attrs.error_message);
        }
      }
    }
    free(mmap_path->send_buffer_list);
  }
  if (mmap_path->recv_buffer_list != NULL) {
    int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      // Free the local destination buffers
      if (mmap_path->send_buffer_list[buf_index].recver_addr_alloced && mmap_path->recv_buffer_list[buf_index].local_mmap_app != NULL) {
        mmapFree(mmap_path->recv_buffer_list[buf_index].local_mmap_app, path->attrs.error_message);
      }
      if (mmap_path->recv_buffer_list[buf_index].local_mmap_flags != NULL) {
        mmapFree(mmap_path->recv_buffer_list[buf_index].local_mmap_flags, path->attrs.error_message);
      }
    }
    free(mmap_path->recv_buffer_list);
  }
}

#ifdef _WIN32
static
#endif
bool tknDestroy(TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = (MmapPath *)private_path->private;

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) destroy path\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  bool graceful_disconnect_ok = false;
  if (mmap_path->connected) {
    // Connection was made, so disconnect gracefully
    graceful_disconnect_ok = true;

    // Stop socket monitoring thread
    // Vebosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
      printf("%s (%s:%s) Stop socket monitoring thread\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect);
    }
    if (!pipeWakeUpSelect(mmap_path->read_pipe_fd, mmap_path->write_pipe_fd, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to stop socket monitoring thread\n");
      graceful_disconnect_ok = false;
    } else {
      if (pthread_join(mmap_path->socket_thread, NULL) != 0) {
        perror("pthread_join");
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to stop socket monitoring thread\n");
        graceful_disconnect_ok = false;
      }
    }
    pipeDestroy(mmap_path->pipe_name, mmap_path->read_pipe_fd, mmap_path->write_pipe_fd);

    // Socket round trip, to make sure both sides are ready to release their resources
    // Vebosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
      printf("%s (%s:%s) Finalize barrier 1\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect);
    }
    if (!socketBarrier(path->attrs.is_endpointA, mmap_path->socket_fd, BARRIER_FINALIZE_STEP1, private_path->destroy_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run finalize barrier\n");
      graceful_disconnect_ok = false;
    }

    // If the barrier completed, the close will not likely hold up any data
    socketClose(mmap_path->socket_fd);
  }

  // IMPORTANT:
  // Seems like it should be safe to free this even if socket disconnected early:
  //  - Socket is local (not TCP), so can't be perturb by disconnecting some cable.
  //  - If other side crashed, then it won't be accessing shared memory or mutex.
  //  - If other side purposely killed the connecting and is not trying to restart, it has to wait for the local socket to connect before accessing shared memory.
  free_path_resources(path, graceful_disconnect_ok);

  // Free the private handle
  free(mmap_path);

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%s (%s:%s) done\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  return graceful_disconnect_ok;
}

#ifdef _WIN32
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

  // Supported formats:
  //   "Mmap -ID <ID> [-share] [-reuse] [-app_alloced_recv_mem] [-remote_mmap_prefix <name>]"
  int path_id;
  bool found;
  bool ok = argGetInt(path->attrs.interconnect, "-ID", &path_id, &found, path->attrs.error_message);
  if (!ok || !found) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "interconnect text missing -ID <value>\n");
    return false;
  }
  // Check if shared pointer or memcpy
  bool is_shared_pointer = argGetFlag(path->attrs.interconnect, "-share");
  // Check if the application will be providing the mmap memory for the remote side
  bool app_alloced_recv_mem = argGetFlag(path->attrs.interconnect, "-app_alloced_recv_mem");
  bool has_remote_mmap_prefix = false;
  char remote_mmap_prefix[MAX_MMAP_NAME_CHARS];
  ok = argGetText(path->attrs.interconnect, "-remote_mmap_prefix", remote_mmap_prefix, MAX_MMAP_NAME_CHARS, &has_remote_mmap_prefix, path->attrs.error_message);
  if (!ok) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to search for interconnect flag -remote_mmap_prefix\n");
    return false;
  }
  // Create local socket name used to coordinate connection and disconnection
  char socket_name[MAX_TAKYON_INTERCONNECT_CHARS];
  sprintf(socket_name, "TakyonMMAP_sock_%d", path_id);
  bool allow_reuse = argGetFlag(path->attrs.interconnect, "-reuse");

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%-15s (%s:%s) create path (id=%d)\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           path_id);
  }

  // Verify dest addresses are all Takyon managed. This is because mmap can't map already created memory.
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  if (!app_alloced_recv_mem) {
    for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
      if (path->attrs.recver_addr_list[buf_index] != 0) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "All addresses in path->attrs.recver_addr_list must be NULL and will be allocated by Takyon, unless the flag '-app_alloced_recv_mem' is set which means the app allocated the receive side memory map addresses for each buffer.\n");
        return false;
      }
    }
  }

  // Verify src addresses are all Takyon managed, only if -share is active.
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  if (is_shared_pointer) {
    for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
      if (path->attrs.sender_addr_list[buf_index] != 0) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "All addresses in path->attrs.sender_addr_list must be NULL since both the src and dest will use the same memory\n");
        return false;
      }
    }
  }

  // Allocate private handle
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  MmapPath *mmap_path = calloc(1, sizeof(MmapPath));
  if (mmap_path == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    return false;
  }
  mmap_path->is_shared_pointer = is_shared_pointer;
  private_path->private = mmap_path;

  // Allocate the buffers
  mmap_path->send_buffer_list = calloc(nbufs_sender, sizeof(XferBuffer));
  if (mmap_path->send_buffer_list == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Out of memory\n");
    goto cleanup;
  }
  mmap_path->recv_buffer_list = calloc(nbufs_recver, sizeof(XferBuffer));
  if (mmap_path->recv_buffer_list == NULL) {
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
        mmap_path->send_buffer_list[buf_index].sender_addr_alloced = true;
      } else {
        mmap_path->send_buffer_list[buf_index].sender_addr = sender_addr;
      }
    }
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    // Recver (this should not be set by application, unless the app specifically set the flag -app_alloced_recv_mem)
    if (app_alloced_recv_mem) {
      uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
      if (recver_bytes > 0) {
        size_t recver_addr = path->attrs.recver_addr_list[buf_index];
        if (recver_addr == 0) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "attrs.recver_addr_list[%d]=0, but the flag '-app_alloced_recv_mem' has been set which means the application must allocate all the buffers with memory mapped addresses.\n", buf_index);
          goto cleanup;
        } else {
          mmap_path->recv_buffer_list[buf_index].local_recver_addr = recver_addr;
        }
      }
    } else {
      mmap_path->recv_buffer_list[buf_index].recver_addr_alloced = true;
    }
  }

  // Create local sender memory (only if not pointer sharing)
  if (!is_shared_pointer) {
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
        mmap_path->send_buffer_list[buf_index].sender_addr = (size_t)addr;
      }
    }
  }

  // Create local unix socket to synchronize the creation
  if (path->attrs.is_endpointA) {
    if (!socketCreateLocalClient(socket_name, &mmap_path->socket_fd, private_path->create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local client socket\n");
      goto cleanup;
    }
  } else {
    if (!socketCreateLocalServer(socket_name, allow_reuse, &mmap_path->socket_fd, private_path->create_timeout_ns, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create local server socket\n");
      goto cleanup;
    }
  }

  // Socket round trip, to make sure the socket is really connected
  if (!socketBarrier(path->attrs.is_endpointA, mmap_path->socket_fd, BARRIER_INIT_STEP1, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 1\n");
    goto cleanup;
  }

  // Verify endpoints have the same attributes
  if (!socketSwapAndVerifyInt(mmap_path->socket_fd, path->attrs.nbufs_AtoB, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_AtoB: (this=%d)\n", path->attrs.nbufs_AtoB);
    goto cleanup;
  }
  if (!socketSwapAndVerifyInt(mmap_path->socket_fd, path->attrs.nbufs_BtoA, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Both endpoints are using a different values for attrs->nbufs_BtoA: (this=%d)\n", path->attrs.nbufs_BtoA);
    goto cleanup;
  }

  // Make sure each sender knows the remote recver buffer sizes
  if (is_shared_pointer) {
    // Sender and recevier must have same size since they are shared
    for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
      uint64_t max_bytes = path->attrs.is_endpointA ? path->attrs.sender_max_bytes_list[i] : path->attrs.recver_max_bytes_list[i];
      if (!socketSwapAndVerifyUInt64(mmap_path->socket_fd, max_bytes, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message,
                            "Both endpoints, sharing A to B buffer, are using a different values for sender_max_bytes_list[%d]=%lld and recver_max_bytes_list[%d]\n",
                            i, (unsigned long long)max_bytes, i);
        goto cleanup;
      }
      // This is shared memory so the local sender and remote receiver are the same
      if (path->attrs.is_endpointA) mmap_path->send_buffer_list[i].remote_max_recver_bytes = path->attrs.sender_max_bytes_list[i];
    }
    for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
      uint64_t max_bytes = path->attrs.is_endpointA ? path->attrs.recver_max_bytes_list[i] : path->attrs.sender_max_bytes_list[i];
      if (!socketSwapAndVerifyUInt64(mmap_path->socket_fd, max_bytes, private_path->create_timeout_ns, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message,
                            "Both endpoints, sharing B to A buffer, are using a different values for recver_max_bytes_list[%d]=%lld and sender_max_bytes_list[%d]\n",
                            i, (unsigned long long)max_bytes, i);
        goto cleanup;
      }
      // This is shared memory so the local sender and remote receiver are the same
      if (!path->attrs.is_endpointA) mmap_path->send_buffer_list[i].remote_max_recver_bytes = path->attrs.sender_max_bytes_list[i];
    }

  } else {
    if (path->attrs.is_endpointA) {
      // Step 1: Endpoint A: send recver sizes to B
      for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
        if (!socketSendUInt64(mmap_path->socket_fd, path->attrs.recver_max_bytes_list[i], private_path->create_timeout_ns, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver buffer[%d] size\n", i);
          goto cleanup;
        }
      }
      // Step 4: Endpoint A: recv recver sizes from B
      for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
        if (!socketRecvUInt64(mmap_path->socket_fd, &mmap_path->send_buffer_list[i].remote_max_recver_bytes, private_path->create_timeout_ns, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver buffer[%d] size\n", i);
          goto cleanup;
        }
      }
    } else {
      // Step 2: Endpoint B: recv recver sizes from A
      for (int i=0; i<path->attrs.nbufs_BtoA; i++) {
        if (!socketRecvUInt64(mmap_path->socket_fd, &mmap_path->send_buffer_list[i].remote_max_recver_bytes, private_path->create_timeout_ns, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to recv recver buffer[%d] size\n", i);
          goto cleanup;
        }
      }
      // Step 3: Endpoint B: send recver sizes to A
      for (int i=0; i<path->attrs.nbufs_AtoB; i++) {
        if (!socketSendUInt64(mmap_path->socket_fd, path->attrs.recver_max_bytes_list[i], private_path->create_timeout_ns, path->attrs.error_message)) {
          TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to send recver buffer[%d] size\n", i);
          goto cleanup;
        }
      }
    }
  }
  
  // Create local recver memory (this is a posix memory map)
  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%s (%s:%s) Create shared destination resources\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }
  for (int buf_index=0; buf_index<nbufs_recver; buf_index++) {
    uint64_t recver_bytes = path->attrs.recver_max_bytes_list[buf_index];
    char local_mmap_name[MAX_MMAP_NAME_CHARS];
    void *addr;
    // Flags
    snprintf(local_mmap_name, MAX_MMAP_NAME_CHARS, "TknMMAP%s_flags_%d_%d", path->attrs.is_endpointA ? "c" : "s", buf_index, path_id);
    if (!mmapAlloc(local_mmap_name, NUM_SHARED_SYNC_BYTES, &addr, &mmap_path->recv_buffer_list[buf_index].local_mmap_flags, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create shared flag memory: %s\n", local_mmap_name);
      goto cleanup;
    }
    mmap_path->recv_buffer_list[buf_index].local_got_data_ref               = (uint64_t *)addr;
    mmap_path->recv_buffer_list[buf_index].local_num_blocks_recved_ref      = (uint64_t *)((size_t)addr + sizeof(uint64_t));
    mmap_path->recv_buffer_list[buf_index].local_bytes_per_block_recved_ref = (uint64_t *)((size_t)addr + sizeof(uint64_t)*2);
    mmap_path->recv_buffer_list[buf_index].local_offset_recved_ref          = (uint64_t *)((size_t)addr + sizeof(uint64_t)*3);
    mmap_path->recv_buffer_list[buf_index].local_stride_recved_ref          = (uint64_t *)((size_t)addr + sizeof(uint64_t)*4);
    *mmap_path->recv_buffer_list[buf_index].local_got_data_ref               = 0;
    *mmap_path->recv_buffer_list[buf_index].local_num_blocks_recved_ref      = 0;
    *mmap_path->recv_buffer_list[buf_index].local_bytes_per_block_recved_ref = 0;
    *mmap_path->recv_buffer_list[buf_index].local_offset_recved_ref          = 0;
    *mmap_path->recv_buffer_list[buf_index].local_stride_recved_ref          = 0;
    // Application memory
    if (app_alloced_recv_mem) {
      // Passed in by the application
      size_t recver_addr = path->attrs.recver_addr_list[buf_index];
      mmap_path->recv_buffer_list[buf_index].local_recver_addr = recver_addr;
    } else {
      // Takyon needs to allocate
      snprintf(local_mmap_name, MAX_MMAP_NAME_CHARS, "TknMMAP%s_app_%d_%lld_%d", path->attrs.is_endpointA ? "c" : "s", buf_index, (unsigned long long)recver_bytes, path_id);
      if (!mmapAlloc(local_mmap_name, recver_bytes, &addr, &mmap_path->recv_buffer_list[buf_index].local_mmap_app, path->attrs.error_message)) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create shared app memory: %s\n", local_mmap_name);
        goto cleanup;
      }
      path->attrs.recver_addr_list[buf_index] = (size_t)addr;
      mmap_path->recv_buffer_list[buf_index].local_recver_addr = (size_t)addr;
    }
  }

  // Create local sync memory (this is a posix memory map for the mutex and conditional variable)
  uint64_t sync_bytes = sizeof(pthread_mutex_t) + sizeof(pthread_cond_t);
  char sync_mmap_name[MAX_MMAP_NAME_CHARS];
  snprintf(sync_mmap_name, MAX_MMAP_NAME_CHARS, "TakyonMMAP_sync_%d", path_id);
  if (path->attrs.is_endpointA) {
    // Vebosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
      printf("%s (%s:%s) Create shared mutex and cond var: sync_mmap_name=%s\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             sync_mmap_name);
    }
    void *addr;
    // Create
    if (!mmapAlloc(sync_mmap_name, sync_bytes, &addr, &mmap_path->shared_sync_mem, path->attrs.error_message)) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create shared sync memory: %s\n", sync_mmap_name);
      goto cleanup;
    }
    mmap_path->mutex = (pthread_mutex_t *)((size_t)addr);
    mmap_path->connection_cond = (pthread_cond_t *)((size_t)addr + sizeof(pthread_mutex_t));
    // Init sync attributes
    int rc;
    pthread_mutexattr_t mutex_attr;
    rc = pthread_mutexattr_init(&mutex_attr);
    if (rc != 0) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create mutex attribute: rc=%d\n", rc); goto cleanup; }
    rc = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create activate process shared mutex attribute: rc=%d\n", rc);
      pthread_mutexattr_destroy(&mutex_attr);
      goto cleanup;
    }
    rc = pthread_mutex_init(mmap_path->mutex, &mutex_attr);
    if (rc != 0) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create initialize process shared mutex: rc=%d\n", rc);
      pthread_mutexattr_destroy(&mutex_attr);
      goto cleanup;
    }
    pthread_mutexattr_destroy(&mutex_attr);
    pthread_condattr_t cond_attr;
    rc = pthread_condattr_init(&cond_attr);
    if (rc != 0) { TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create condvar attribute: rc=%d\n", rc); goto cleanup; }
    rc = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create activate process shared condvar attribute: rc=%d\n", rc);
      pthread_condattr_destroy(&cond_attr);
      goto cleanup;
    }
    rc = pthread_cond_init(mmap_path->connection_cond, &cond_attr);
    if (rc != 0) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create initialize process shared condvar: rc=%d\n", rc);
      pthread_condattr_destroy(&cond_attr);
      goto cleanup;
    }
    pthread_condattr_destroy(&cond_attr);
  }

  // Socket round trip, to make sure the remote memory handle is the latest and not stale
  if (!socketBarrier(path->attrs.is_endpointA, mmap_path->socket_fd, BARRIER_INIT_STEP2, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 2\n");
    goto cleanup;
  }

  // Get the remote destination address
  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%s (%s:%s) Get remote memory addresses\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }
  for (int buf_index=0; buf_index<nbufs_sender; buf_index++) {
    uint64_t remote_recver_bytes = mmap_path->send_buffer_list[buf_index].remote_max_recver_bytes;
    char remote_mmap_name[MAX_MMAP_NAME_CHARS];
    void *addr;
    bool timed_out;
    // Flags
    snprintf(remote_mmap_name, MAX_MMAP_NAME_CHARS, "TknMMAP%s_flags_%d_%d", path->attrs.is_endpointA ? "s" : "c", buf_index, path_id);
    bool success = mmapGetTimed(remote_mmap_name, NUM_SHARED_SYNC_BYTES, &addr, &mmap_path->send_buffer_list[buf_index].remote_mmap_flags, private_path->create_timeout_ns, &timed_out, path->attrs.error_message);
    if ((!success) || timed_out) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get handle to remote shared flag memory: %s\n", remote_mmap_name);
      goto cleanup;
    }
    mmap_path->send_buffer_list[buf_index].remote_got_data_ref               = (uint64_t *)addr;
    mmap_path->send_buffer_list[buf_index].remote_num_blocks_recved_ref      = (uint64_t *)((size_t)addr + sizeof(uint64_t));
    mmap_path->send_buffer_list[buf_index].remote_bytes_per_block_recved_ref = (uint64_t *)((size_t)addr + sizeof(uint64_t)*2);
    mmap_path->send_buffer_list[buf_index].remote_offset_recved_ref          = (uint64_t *)((size_t)addr + sizeof(uint64_t)*3);
    mmap_path->send_buffer_list[buf_index].remote_stride_recved_ref          = (uint64_t *)((size_t)addr + sizeof(uint64_t)*4);
    // Application memory
    if (has_remote_mmap_prefix) {
      snprintf(remote_mmap_name, MAX_MMAP_NAME_CHARS, "%s%d", remote_mmap_prefix, buf_index);
    } else {
      snprintf(remote_mmap_name, MAX_MMAP_NAME_CHARS, "TknMMAP%s_app_%d_%lld_%d", path->attrs.is_endpointA ? "s" : "c", buf_index, (unsigned long long)remote_recver_bytes, path_id);
    }
    success = mmapGetTimed(remote_mmap_name, remote_recver_bytes, &addr, &mmap_path->send_buffer_list[buf_index].remote_mmap_app, private_path->create_timeout_ns, &timed_out, path->attrs.error_message);
    if ((!success) || timed_out) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get handle to remote shared app memory: %s\n", remote_mmap_name);
      goto cleanup;
    }
    mmap_path->send_buffer_list[buf_index].remote_recver_addr = (size_t)addr;
    if (is_shared_pointer) {
      // Need the sender addr to be the same as the destination address
      path->attrs.sender_addr_list[buf_index] = (size_t)addr;
      mmap_path->send_buffer_list[buf_index].sender_addr = (size_t)addr;
    }
  }

  // Wait for the client to pass the mutex and cond var to the server
  if (!path->attrs.is_endpointA) {
    // Vebosity
    if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
      printf("%s (%s:%s) Wait for shared mutex and cond var: sync_mmap_name=%s\n",
             __FUNCTION__,
             path->attrs.is_endpointA ? "A" : "B",
             path->attrs.interconnect,
             sync_mmap_name);
    }
    void *addr;
    bool timed_out;
    // Get the address
    bool success = mmapGetTimed(sync_mmap_name, sync_bytes, &addr, &mmap_path->shared_sync_mem, private_path->create_timeout_ns, &timed_out, path->attrs.error_message);
    if ((!success) || timed_out) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get handle to remote shared sync memory: %s\n", sync_mmap_name);
      goto cleanup;
    }
    mmap_path->mutex = (pthread_mutex_t *)((size_t)addr);
    mmap_path->connection_cond = (pthread_cond_t *)((size_t)addr + sizeof(pthread_mutex_t));
  }

  // Socket round trip, to make sure the create is completed on both sides and the remote side has not had a change to destroy any of the resources
  if (!socketBarrier(path->attrs.is_endpointA, mmap_path->socket_fd, BARRIER_INIT_STEP3, private_path->create_timeout_ns, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to run init barrier part 3\n");
    goto cleanup;
  }

  // At this point the socket will not be used again until the destroy
  // Create pipe to later wake up thread
  snprintf(mmap_path->pipe_name, MAX_PIPE_NAME_CHARS, "TakyonMMAPpipe_%d", path_id);
  if (!pipeCreate(mmap_path->pipe_name, &mmap_path->read_pipe_fd, &mmap_path->write_pipe_fd, path->attrs.error_message)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create synchronization pipe\n");
    goto cleanup;
  }
  // Create thread to monitor if the socket breaks
  if (pthread_create(&mmap_path->socket_thread, NULL, socket_disconnect_handler, path) != 0) {
    perror("pthread_create");
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to start socket monitoring thread\n");
    goto cleanup;
  }

  mmap_path->connected = true;

  // Vebosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT_DETAILS) {
    printf("%s (%s:%s) Completed connection\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  return true;

 cleanup:
  // An error ocurred so clean up all allocated resources
  free_path_resources(path, true);
  free(mmap_path);
  return false;
}

#ifdef _WIN32
void setMmapFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknSendStrided = tknSendStrided;
  private_path->tknSendTest = tknSendTest;
  private_path->tknRecv = tknRecv;
  private_path->tknRecvStrided = tknRecvStrided;
  private_path->tknDestroy = tknDestroy;
}
#endif
