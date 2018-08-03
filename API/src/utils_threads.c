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
//   Some helpful thread based functionality:
//     - a thread manager to coordinate two threads trying to connect with
//       eachother
//     - a portable condiational wait
// -----------------------------------------------------------------------------

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif
#include <errno.h>
#include "takyon_private.h"

//#define DEBUG_MESSAGE

static pthread_once_t L_once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t *L_master_mutex = NULL;
static pthread_cond_t *L_master_cond = NULL;
static ThreadManagerItem **L_manager_items = NULL;
static unsigned int L_num_manager_items = 0;
static unsigned int L_max_manager_items = 0;

static struct timespec getCurrentAbsoluteTime(int64_t timeout_ns) {
  struct timespec future_time;
#define NANOSECONDS_PER_SECOND 1000000000
  unsigned timeout_secs = (unsigned)(timeout_ns / NANOSECONDS_PER_SECOND);
  unsigned timeout_nsecs = (unsigned)(timeout_ns % NANOSECONDS_PER_SECOND);

#ifdef _WIN32
  struct _timeb curr_sys_time;
  /* Get the current time since 1970 */
  _ftime(&curr_sys_time);
  future_time.tv_sec = timeout_secs + (unsigned)curr_sys_time.time;
  future_time.tv_nsec = timeout_nsecs + curr_sys_time.millitm * 1000000;
#else
  struct timeval ctp;
  gettimeofday(&ctp, NULL);
  future_time.tv_sec  = (unsigned)(timeout_secs + ctp.tv_sec);
  future_time.tv_nsec = (unsigned)(timeout_nsecs + ctp.tv_usec*1000);
#endif

  /* Do some overlap correction if needed */
  while (future_time.tv_nsec >= NANOSECONDS_PER_SECOND) {
    future_time.tv_nsec -= NANOSECONDS_PER_SECOND;
    future_time.tv_sec++;
  }
  if (future_time.tv_nsec < 0) {
    future_time.tv_nsec = 0x7fffffff;
  }

  return future_time;
}

bool threadCondWait(pthread_mutex_t *mutex, pthread_cond_t *cond_var, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  int rc;

  if (timed_out_ret != NULL) *timed_out_ret = false;

  if (timeout_ns < 0) {
#ifdef __APPLE__
    int retry_count = 0;
  retry:
#endif
    rc = pthread_cond_wait(cond_var, mutex);
    if (rc != 0) {
#ifdef __APPLE__
      if ((errno == ETIMEDOUT) || (errno == ENOENT)) {
        if (retry_count < 10) {
          int64_t microseconds = 1;
          clockSleepYield(microseconds);
          retry_count++;
          goto retry;
        }
      }
#endif
      perror("pthread_cond_wait");
      TAKYON_RECORD_ERROR(error_message, "Failed to call pthread_cond_wait(), rc=%d, errno=%d\n", rc, errno);
      return false;
    }
    return true;

  } else {
    struct timespec future_time = getCurrentAbsoluteTime(timeout_ns);
    /* Wait until signaled or timeout */
    rc = pthread_cond_timedwait(cond_var, mutex, &future_time);
    if (rc == 0) {
      /* Returned succesfully */
      return true;
    } else if (rc == ETIMEDOUT) {
      if (timed_out_ret != NULL) *timed_out_ret = true;
      return true;
    } else {
      TAKYON_RECORD_ERROR(error_message, "Failed to call pthread_cond_timedwait()\n");
      return false;
    }
  }
}

static void threadManagerFinalize(void) {
#ifdef DEBUG_MESSAGE
  printf("Finalizing the thread manager\n");
#endif
  if (L_manager_items != NULL) {
    free(L_manager_items);
    L_manager_items = NULL;
  }
  if (L_master_mutex != NULL) {
    pthread_mutex_destroy(L_master_mutex);
    free(L_master_mutex);
    L_master_mutex = NULL;
  }
  if (L_master_cond != NULL) {
    pthread_cond_destroy(L_master_cond);
    free(L_master_cond);
    L_master_cond = NULL;
  }
  L_num_manager_items = 0;
  L_max_manager_items = 0;
#ifdef DEBUG_MESSAGE
  printf("Done finalizing the thread manager\n");
#endif
}

static void threadManagerInitOnce(void) {
#ifdef DEBUG_MESSAGE
  printf("Initializing the thread manager\n");
#endif
  L_max_manager_items = 1;
  L_manager_items = calloc(L_max_manager_items, sizeof(ThreadManagerItem));

  L_master_mutex = calloc(1, sizeof(pthread_mutex_t));
  if (L_master_mutex == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  pthread_mutex_init(L_master_mutex, NULL);

  L_master_cond = calloc(1, sizeof(pthread_cond_t));
  if (L_master_cond == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  pthread_cond_init(L_master_cond, NULL);

  // This will get called if the app calls exit() or if main does a normal return.
  if (atexit(threadManagerFinalize) == -1) {
    fprintf(stderr, "Failed to setup atexit() in the thread manager initializer\n");
    exit(EXIT_FAILURE);
  }
#ifdef DEBUG_MESSAGE
  printf("Done initializing the thread manager\n");
#endif
}

bool threadManagerInit() {
  // Call this to make sure the mutex manager is ready to coordinate: This can be called multiple times, but it's garanteed to atomically run the only the first time called.
  if (pthread_once(&L_once_control, threadManagerInitOnce) != 0) {
    fprintf(stderr, "Failed to start the mutex manager\n");
    return false;
  }
  return true;
}

static void threadManagerAddItem(ThreadManagerItem *item) {
#ifdef DEBUG_MESSAGE
  printf("Thread Manager: adding item with interconnect id %d and id %d\n", item->interconnect_id, item->path_id);
#endif

  if (L_num_manager_items == L_max_manager_items) {
    // List is full so double the size of the list
    L_max_manager_items *= 2;
    L_manager_items = realloc(L_manager_items, L_max_manager_items*sizeof(ThreadManagerItem *));
    if (L_manager_items == NULL) {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
  }

  // Add new item to first empty slot
  L_manager_items[L_num_manager_items] = item;
  L_num_manager_items++;
}

static ThreadManagerItem *getUnconnectedManagerItem(int interconnect_id, int path_id) {
#ifdef DEBUG_MESSAGE
  printf("Thread Manager: find item with interconnect id %d and id %d\n", interconnect_id, path_id);
#endif
  for (unsigned int i=0; i<L_num_manager_items; i++) {
    if ((L_manager_items[i]->connected == false) && (L_manager_items[i]->interconnect_id == interconnect_id) && (L_manager_items[i]->path_id == path_id)) {
      return L_manager_items[i];
    }
  }
  return NULL;
}

static void threadManagerRemoveItem(ThreadManagerItem *item) {
#ifdef DEBUG_MESSAGE
  printf("Thread Manager: remove item with interconnect id %d and id %d\n", item->interconnect_id, item->path_id);
#endif
  for (unsigned int i=0; i<L_num_manager_items; i++) {
    if (L_manager_items[i] == item) {
      for (unsigned int j=i+1; j<L_num_manager_items; j++) {
        L_manager_items[j-1] = L_manager_items[j];
      }
      break;
    }
  }
  L_num_manager_items--;
}

ThreadManagerItem *threadManagerConnect(int interconnect_id, int path_id, TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;
  ThreadManagerItem *item = NULL;

  if (path->attrs.is_endpointA) {
#ifdef DEBUG_MESSAGE
    printf("Thread Manager: endpoint A waiting to connect: interconnect id %d and id %d\n", interconnect_id, path_id);
#endif
    // Lock mutex
    pthread_mutex_lock(L_master_mutex);
    // Verify not already in use
    item = getUnconnectedManagerItem(interconnect_id, path_id);
    if (item != NULL) {
      pthread_mutex_unlock(L_master_mutex);
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Endpoint A inter-thread connection failure. Interconnect ID=%d and path ID=%d are already in use.\n", interconnect_id, path_id);
      return NULL;
    }
    // Create the new item
    item = (ThreadManagerItem *)calloc(1, sizeof(ThreadManagerItem));
    if (item == NULL) {
      pthread_mutex_unlock(L_master_mutex);
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
    item->interconnect_id = interconnect_id;
    item->path_id = path_id;
    item->pathA = path;
    item->pathB = NULL;

    // Create the mutex and cond var
    int rc;
    rc = pthread_mutex_init(&item->mutex, NULL);
    if (rc != 0) {
      free(item);
      pthread_mutex_unlock(L_master_mutex);
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create mutex.\n");
      return NULL;
    }
    rc = pthread_cond_init(&item->cond, NULL);
    if (rc != 0) {
      pthread_mutex_destroy(&item->mutex);
      free(item);
      pthread_mutex_unlock(L_master_mutex);
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to create conditional variable.\n");
      return NULL;
    }

    item->connected = false;
    item->disconnected = false;
    item->connection_broken = false;
    item->memory_buffers_syned = false;
    item->usage_count = 0;
    // Add to mutex manager list
    threadManagerAddItem(item);
    // Wake up all threads waiting on the mutex manager
    pthread_cond_broadcast(L_master_cond);
    // Wait for endpoint B to connect
    while (!item->connected) {
      bool timed_out;
      bool suceeded = threadCondWait(L_master_mutex, L_master_cond, private_path->create_timeout_ns, &timed_out, path->attrs.error_message);
      if ((!suceeded) || timed_out) {
        // An error occured or it timed out with no connection
        threadManagerRemoveItem(item);
        pthread_mutex_destroy(&item->mutex);
        pthread_cond_destroy(&item->cond);
        pthread_mutex_unlock(L_master_mutex);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Endpoint A inter-thread connection failure. Failed to wait for endpoint B to connect\n");
        return NULL;
      }
    }
    pthread_mutex_unlock(L_master_mutex);

  } else {
#ifdef DEBUG_MESSAGE
    printf("Thread Manager: endpoint B waiting to connect: interconnect id %d and id %d\n", interconnect_id, path_id);
#endif
    pthread_mutex_lock(L_master_mutex);
    // See if item already exists
    item = getUnconnectedManagerItem(interconnect_id, path_id);
    while (item == NULL) {
      // Give some time for endpointA to connect
      bool timed_out;
      bool suceeded = threadCondWait(L_master_mutex, L_master_cond, private_path->create_timeout_ns, &timed_out, path->attrs.error_message);
      if ((!suceeded) || timed_out) {
        // An error or timeout occured
        pthread_mutex_unlock(L_master_mutex);
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Endpoint B inter-thread connection failure. Failed to wait for endpoint A to connect\n");
        return NULL;
      }
      item = getUnconnectedManagerItem(interconnect_id, path_id);
    }
    // Fill in the remote details
    item->pathB = path;
    item->usage_count = 2;
    item->connected = true;
    // Wake up endpoint A: note that this wakes up all threads
    pthread_cond_broadcast(L_master_cond);
    // Unlock before returning
    pthread_mutex_unlock(L_master_mutex);
  }

  return item;
}

void threadManagerMarkConnectionAsBad(ThreadManagerItem *item) {
  // Should already be locked: pthread_mutex_lock(&item->mutex);
  item->connection_broken = true;
  pthread_cond_signal(&item->cond); // Wake up remote thread if waiting to disconnect
  // pthread_mutex_unlock(&item->mutex);
}

bool threadManagerDisconnect(TakyonPath *path, ThreadManagerItem *item) {
  bool graceful_disconnect = true;
  pthread_mutex_lock(&item->mutex);
  item->usage_count--;
  if (item->usage_count == 0) {
    pthread_cond_signal(&item->cond); // Wake up remote thread if waiting to disconnect
    // The remote side may not have finished it's disconnect
    // Wait for the remote side to finish (even if it's not a graceful disconnect)
    while (!item->disconnected) {
      pthread_cond_wait(&item->cond, &item->mutex);
    }
  } else if (item->usage_count > 0) {
    while ((!item->disconnected) && (item->usage_count > 0)) {
      // This path is in a good state (no errors on the path, so wait for the remote side to disconnect
      TakyonPrivatePath *private_path = path->attrs.is_endpointA ? (TakyonPrivatePath *)item->pathA->private : (TakyonPrivatePath *)item->pathB->private;
      bool timed_out;
      bool suceeded = threadCondWait(&item->mutex, &item->cond, private_path->destroy_timeout_ns, &timed_out, path->attrs.error_message);
      if (!suceeded) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to wait for remote endpoint to disconnect\n");
        graceful_disconnect = false;
      } else if (timed_out) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Timed out waiting for remote endpoint to disconnect\n");
        graceful_disconnect = false;
      }
      if ((!suceeded) || timed_out) {
        // An error occured or it timed out with no connection
        break;
      }
    }
    item->connection_broken = true;
    item->disconnected = true;
    pthread_cond_signal(&item->cond); // Wake up remote thread if waiting to disconnect
    pthread_mutex_unlock(&item->mutex);
    return graceful_disconnect;
  }
  pthread_mutex_unlock(&item->mutex);

  // Both threads have entered this call. Time to clean up
  pthread_mutex_lock(L_master_mutex);
  threadManagerRemoveItem(item);
  pthread_mutex_unlock(L_master_mutex);

  pthread_mutex_destroy(&item->mutex);
  pthread_cond_destroy(&item->cond);
  free(item);

  return graceful_disconnect;
}
