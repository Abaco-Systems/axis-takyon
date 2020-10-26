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

#include "takyon_private.h"

//#define DEBUG_MESSAGE

/*+ don't do this if VxWorks */
#define USE_AT_EXIT_METHOD  // This is a cleaner way IMHO to handle cleaning up resources, but only works if the OS supports atexit().

#ifdef USE_AT_EXIT_METHOD
static pthread_once_t L_once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t *L_master_mutex = NULL;
static pthread_cond_t *L_master_cond = NULL;
#else
static uint32_t L_usage_counter = 0;
static pthread_mutex_t L_master_mutex_private = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t L_master_cond_private = PTHREAD_COND_INITIALIZER;
#define L_master_mutex (&L_master_mutex_private)
#define L_master_cond (&L_master_cond_private)
#endif
static InterThreadManagerItem **L_manager_items = NULL;
static unsigned int L_num_manager_items = 0;
static unsigned int L_max_manager_items = 0;

static void threadManagerFinalize(void) {
#ifdef DEBUG_MESSAGE
  printf("Finalizing the inter thread manager\n");
#endif
  if (L_manager_items != NULL) {
    free(L_manager_items);
    L_manager_items = NULL;
  }
#ifdef USE_AT_EXIT_METHOD
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
#endif
  L_num_manager_items = 0;
  L_max_manager_items = 0;
#ifdef DEBUG_MESSAGE
  printf("Done finalizing the inter thread manager\n");
#endif
}

#ifdef USE_AT_EXIT_METHOD
static void threadManagerInitOnce(void) {
#ifdef DEBUG_MESSAGE
  printf("Initializing the inter thread manager\n");
#endif
  L_max_manager_items = 1;
  L_manager_items = calloc(L_max_manager_items, L_max_manager_items*sizeof(InterThreadManagerItem *));

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
    fprintf(stderr, "Failed to setup atexit() in the inter thread manager initializer\n");
    exit(EXIT_FAILURE);
  }
#ifdef DEBUG_MESSAGE
  printf("Done initializing the inter thread manager\n");
#endif
}
#endif

bool interThreadManagerInit() {
#ifdef USE_AT_EXIT_METHOD
  // Call this to make sure the inter thread manager is ready to coordinate: This can be called multiple times, but it's guaranteed to atomically run only the first time called.
  if (pthread_once(&L_once_control, threadManagerInitOnce) != 0) {
    fprintf(stderr, "Failed to start the inter thread manager\n");
    return false;
  }
#else
  // Increase usage counter
  pthread_mutex_lock(L_master_mutex);
  L_usage_counter++;
  pthread_mutex_unlock(L_master_mutex);
#endif
  return true;
}

void interThreadManagerFinalize() {
#ifdef USE_AT_EXIT_METHOD
  // Nothing to do
#else
  // Decrease usage counter
  pthread_mutex_lock(L_master_mutex);
  if (L_usage_counter == 0) {
    fprintf(stderr, "interThreadManagerFinalize() was called more times than interThreadManagerInit(). This should never happen.\n");
    pthread_mutex_unlock(L_master_mutex);
    exit(EXIT_FAILURE);
  }
  L_usage_counter--;
  if (L_usage_counter == 0) {
    threadManagerFinalize();
  }
  pthread_mutex_unlock(L_master_mutex);
#endif
}

static void threadManagerAddItem(InterThreadManagerItem *item) {
  // NOTE: this function should only be called when the global mutex is locked
#ifdef DEBUG_MESSAGE
  printf("Inter Thread Manager: adding item with interconnect id %d and id %d\n", item->interconnect_id, item->path_id);
#endif

  if (L_num_manager_items == L_max_manager_items) {
    // Increase the size of the list
    if (L_max_manager_items == 0) {
      // First time the list has been allocated
      L_max_manager_items = 1;
      L_manager_items = calloc(L_max_manager_items, L_max_manager_items*sizeof(InterThreadManagerItem *));
    } else {
      // List is full so double the size of the list
      L_max_manager_items *= 2;
      L_manager_items = realloc(L_manager_items, L_max_manager_items*sizeof(InterThreadManagerItem *));
    }
    if (L_manager_items == NULL) {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
  }

  // Add new item to first empty slot
  L_manager_items[L_num_manager_items] = item;
  L_num_manager_items++;
}

static InterThreadManagerItem *getUnconnectedManagerItem(uint32_t interconnect_id, uint32_t path_id) {
  // NOTE: this function should only be called when the global mutex is locked
#ifdef DEBUG_MESSAGE
  printf("Inter Thread Manager: find item with interconnect id %d and id %d\n", interconnect_id, path_id);
#endif
  for (unsigned int i=0; i<L_num_manager_items; i++) {
    if ((L_manager_items[i]->connected == false) && (L_manager_items[i]->interconnect_id == interconnect_id) && (L_manager_items[i]->path_id == path_id)) {
      return L_manager_items[i];
    }
  }
  return NULL;
}

static void threadManagerRemoveItem(InterThreadManagerItem *item) {
  // NOTE: this function should only be called when the global mutex is locked
#ifdef DEBUG_MESSAGE
  printf("Inter Thread Manager: remove item with interconnect id %d and id %d\n", item->interconnect_id, item->path_id);
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

InterThreadManagerItem *interThreadManagerConnect(uint32_t interconnect_id, uint32_t path_id, TakyonPath *path) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;
  InterThreadManagerItem *item = NULL;

  if (path->attrs.is_endpointA) {
#ifdef DEBUG_MESSAGE
    printf("Inter Thread Manager: endpoint A waiting to connect: interconnect id %d and id %d\n", interconnect_id, path_id);
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
    item = (InterThreadManagerItem *)calloc(1, sizeof(InterThreadManagerItem));
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
    item->usage_count = 0;
    // Add to mutex manager list
    threadManagerAddItem(item);
    // Wake up all threads waiting on the mutex manager
    pthread_cond_broadcast(L_master_cond);
    // Wait for endpoint B to connect
    while (!item->connected) {
      bool timed_out;
      bool suceeded = threadCondWait(L_master_mutex, L_master_cond, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
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
    printf("Inter Thread Manager: endpoint B waiting to connect: interconnect id %d and id %d\n", interconnect_id, path_id);
#endif
    pthread_mutex_lock(L_master_mutex);
    // See if item already exists
    item = getUnconnectedManagerItem(interconnect_id, path_id);
    while (item == NULL) {
      // Give some time for endpointA to connect
      bool timed_out;
      bool suceeded = threadCondWait(L_master_mutex, L_master_cond, private_path->path_create_timeout_ns, &timed_out, path->attrs.error_message);
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

void interThreadManagerMarkConnectionAsBad(InterThreadManagerItem *item) {
  // Should already be locked: pthread_mutex_lock(&item->mutex);
  item->connection_broken = true;
  pthread_cond_signal(&item->cond); // Wake up remote thread if waiting to disconnect
  // pthread_mutex_unlock(&item->mutex);
}

bool interThreadManagerDisconnect(TakyonPath *path, InterThreadManagerItem *item) {
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
      TakyonPrivatePath *private_path = path->attrs.is_endpointA ? (TakyonPrivatePath *)item->pathA->private_path : (TakyonPrivatePath *)item->pathB->private_path;
      bool timed_out;
      bool suceeded = threadCondWait(&item->mutex, &item->cond, private_path->path_destroy_timeout_ns, &timed_out, path->attrs.error_message);
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
