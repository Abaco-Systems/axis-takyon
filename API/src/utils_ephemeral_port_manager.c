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

#define TAKYON_MULTICAST_IP    "127.0.0.1"    // A local interface that is multicast capable (for both sending and receiving)
#define TAKYON_MULTICAST_PORT  6736           // Uses phone digits to spell "Open"
#define TAKYON_MULTICAST_GROUP "229.82.29.66" // Uses phone digits to spell "Takyon" i.e. 229.TA.KY.ON
#define TAKYON_MULTICAST_TTL   1              // Restrict to same subnet

//#define DEBUG_MESSAGE
//#define WARNING_MESSAGE
#define MAX_ITEM_LIFESPAN_SECONDS 5.0
#define REQUEST_TIMEOUT_NS 1000000 // 1 millisecond
#define COND_WAIT_TIMEOUT_NS 1000000000 // 1 second
#define CHECK_FOR_EXIT_TIMEOUT_NS (1000000000/4) // 1/4 second

enum {
  NEW_EPHEMERAL_PORT = 33,
  REQUEST_EPHEMERAL_PORT,
  EPHEMERAL_PORT_CONNECTED
};

typedef struct {
  bool in_use;
  uint32_t path_id;
  uint16_t ephemeral_port_number;
  double timestamp;
  char interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS];
} EphemeralPortManagerItem;

#pragma pack(push, 1)  // Make sure no gaps in data structure
typedef struct {
  unsigned char command;
  unsigned char is_big_endian;
  uint16_t ephemeral_port_number;
  uint32_t path_id;
  char interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS];
} EphemeralPortMessage;
#pragma pack(pop)

static pthread_once_t L_once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t *L_mutex = NULL;
static pthread_cond_t *L_cond = NULL;
static EphemeralPortManagerItem *L_manager_items = NULL;
static uint32_t L_num_manager_items = 0;
static char L_error_message[MAX_ERROR_MESSAGE_CHARS]; // This manager is a global resource not always connected to a path, so need a separate error_message buffer
static TakyonSocket L_multicast_send_socket;
static void *L_multicast_send_socket_in_addr = NULL;
static TakyonSocket L_multicast_recv_socket;
#ifdef _WIN32
static bool L_thread_running = false;
#else
static int L_read_pipe_fd;
static int L_write_pipe_fd;
#endif
static pthread_t L_thread_id;
static uint64_t L_verbosity = TAKYON_VERBOSITY_NONE;

static void addItem(const char *interconnect_name, uint32_t path_id, uint16_t ephemeral_port_number) {
  // Check if already in list
  for (uint32_t i=0; i<L_num_manager_items; i++) {
    EphemeralPortManagerItem *item = &L_manager_items[i];
    if (item->in_use && item->path_id == path_id && strcmp(item->interconnect_name, interconnect_name)==0) {
      if (item->ephemeral_port_number != ephemeral_port_number) {
        // A path with the interconnect and ID might have been shut down and restarted
        item->ephemeral_port_number = ephemeral_port_number;
      }
      // Already in list
      return;
    }
  }

  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("Ephemeral port manager: adding item (interconnect='%s', path_id=%u, ephemeral_port_number=%hu)\n", interconnect_name, path_id, ephemeral_port_number);
  }

  // See if there is an unused item
  EphemeralPortManagerItem *item = NULL;
  for (uint32_t i=0; i<L_num_manager_items; i++) {
    if (!L_manager_items[i].in_use) {
      // Found an empty item
      item = &L_manager_items[i];
      break;
    }
  }

  // Increase the size of the list if it's full
  if (item == NULL) {
    L_manager_items = realloc(L_manager_items, 2*L_num_manager_items*sizeof(EphemeralPortManagerItem));
    if (L_manager_items == NULL) {
      fprintf(stderr, "Out of memory\n");
      exit(EXIT_FAILURE);
    }
    for (uint32_t i=L_num_manager_items; i<L_num_manager_items*2; i++) {
      L_manager_items[i].in_use = false;
    }
    item = &L_manager_items[L_num_manager_items];
    L_num_manager_items *= 2;
  }

  // Add new item
  item->in_use = true;
  item->path_id = path_id;
  item->ephemeral_port_number = ephemeral_port_number;
  item->timestamp = clockTimeNanoseconds() / NANOSECONDS_PER_SECOND_DOUBLE;
  strncpy(item->interconnect_name, interconnect_name, TAKYON_MAX_INTERCONNECT_CHARS-1);
  item->interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS-1] = '\0';
}

static bool hasItem(const char *interconnect_name, uint32_t path_id, uint16_t *ephemeral_port_number_ret) {
  for (uint32_t i=0; i<L_num_manager_items; i++) {
    EphemeralPortManagerItem *item = &L_manager_items[i];
    if (item->in_use && item->path_id == path_id && strcmp(item->interconnect_name, interconnect_name)==0) {
      *ephemeral_port_number_ret = item->ephemeral_port_number;
      return true;
    }
  }
  return false;
}

static void removeItem(const char *interconnect_name, uint32_t path_id) {
#ifdef DEBUG_MESSAGE
  printf("Ephemeral port manager: removing item (interconnect='%s', path_id=%u)\n", interconnect_name, path_id);
#endif
  double curr_time = clockTimeNanoseconds() / NANOSECONDS_PER_SECOND_DOUBLE;
  for (uint32_t i=0; i<L_num_manager_items; i++) {
    EphemeralPortManagerItem *item = &L_manager_items[i];
    if (item->in_use) {
      double lifespan = curr_time - item->timestamp;
      if (lifespan > MAX_ITEM_LIFESPAN_SECONDS || (item->path_id == path_id && strcmp(item->interconnect_name, interconnect_name)==0)) {
        item->in_use = false;
      }
    }
  }
}

static void *ephemeralPortMonitoringThread(void *user_arg) {
  bool this_is_big_endian = endianIsBig();

  while (true) {
#ifdef _WIN32
    // Wait for a multicast message to arrive
    int64_t timeout_ns = CHECK_FOR_EXIT_TIMEOUT_NS;
    bool is_polling = false;
    uint64_t bytes_read;
    EphemeralPortMessage message;
    bool timed_out = false;
#ifdef DEBUG_MESSAGE
    printf("Ephemeral port manager: Waiting for multicast message\n");
#endif
    if (!socketDatagramRecv(L_multicast_recv_socket, &message, sizeof(EphemeralPortMessage), &bytes_read, is_polling, timeout_ns, &timed_out, L_error_message)) {
      // Failed to read the message
      fprintf(stderr, "Ephemeral port manager thread: Failed to read pending datagram: %s\n", L_error_message);
      exit(EXIT_FAILURE);
    }
    if (!L_thread_running) {
      // Time to gracefully exit
      break;
    }
    if (timed_out) {
      // No message so try again
      continue;
    }
#ifdef DEBUG_MESSAGE
    printf("Ephemeral port manager: Got multicast message\n");
#endif
#else

    // Wait for a multicast message to arrive
#ifdef DEBUG_MESSAGE
    printf("Ephemeral port manager: Waiting for multicast message\n");
#endif
    bool got_socket_activity;
    bool ok = socketWaitForDisconnectActivity(L_multicast_recv_socket, L_read_pipe_fd, &got_socket_activity, L_error_message);
    if (!ok) {
      fprintf(stderr, "Ephemeral port manager thread: Failed to wait for activity on the multicast receive socket: %s\n", L_error_message);
      exit(EXIT_FAILURE);
    }
    if (!got_socket_activity) {
      // The read pipe has woken up this thread
      // Time to gracefully exit
      break;
    }

    // A multicast message arrived, so read it
#ifdef DEBUG_MESSAGE
    printf("Ephemeral port manager: Reading multicast message\n");
#endif
    int64_t timeout_ns = TAKYON_WAIT_FOREVER;
    bool is_polling = false;
    uint64_t bytes_read;
    EphemeralPortMessage message;
    if (!socketDatagramRecv(L_multicast_recv_socket, &message, sizeof(EphemeralPortMessage), &bytes_read, is_polling, timeout_ns, NULL, L_error_message)) {
      // Failed to read the message
      fprintf(stderr, "Ephemeral port manager thread: Failed to read pending datagram: %s\n", L_error_message);
      exit(EXIT_FAILURE);
    }
#endif

    // Interpret the message
    if (message.command != NEW_EPHEMERAL_PORT && message.command != REQUEST_EPHEMERAL_PORT && message.command != EPHEMERAL_PORT_CONNECTED) {
      fprintf(stderr, "Ephemeral port manager thread: Got invalid command = %d\n", message.command);
      exit(EXIT_FAILURE);
    }
    if (this_is_big_endian != message.is_big_endian) {
      // Endian swap the port number and path ID
      endianSwapUInt16(&message.ephemeral_port_number, 1);
      endianSwapUInt32(&message.path_id, 1);
    }
    if (message.command == NEW_EPHEMERAL_PORT) {
      // Record the info and wake up any threads waiting for it
      pthread_mutex_lock(L_mutex);
      addItem(message.interconnect_name, message.path_id, message.ephemeral_port_number);
      pthread_mutex_unlock(L_mutex);
      // Wake up all threads waiting on an ephemeral port number
      pthread_cond_broadcast(L_cond);
    } else if (message.command == REQUEST_EPHEMERAL_PORT) {
      pthread_mutex_lock(L_mutex);
      if (hasItem(message.interconnect_name, message.path_id, &message.ephemeral_port_number)) {
        // Exists in database, so re-multicast it
#ifdef DEBUG_MESSAGE
        printf("Ephemeral port manager: Re-multicast (interconnect='%s', path_id=%u, ephemeral_port_number=%hu)\n", message.interconnect_name, message.path_id, message.ephemeral_port_number);
#endif
        message.command = NEW_EPHEMERAL_PORT;
        message.is_big_endian = this_is_big_endian;
        // NOTE: this send is within a mutex, but this port manager is not used once a coonection is made and will not perturb communication performance
        int64_t heartbeat_timeout_ns = REQUEST_TIMEOUT_NS; // Need some time to get the message out, so ignore the path's timeout period
        bool timed_out = false;
        if (!socketDatagramSend(L_multicast_send_socket, L_multicast_send_socket_in_addr, &message, sizeof(EphemeralPortMessage), is_polling, heartbeat_timeout_ns, &timed_out, L_error_message)) {
#ifdef WARNING_MESSAGE
          fprintf(stderr, "Warning: Ephemeral port manager thread: Failed to re-multicast ephemeral port info: %s. Will eventually retry\n", L_error_message);
#endif          
        }
        // NOTE: if timed out, just ignore since a subsequent attempt will be done if needed
      }
      pthread_mutex_unlock(L_mutex);
    } else if (message.command == EPHEMERAL_PORT_CONNECTED) {
      // Remove item from the list
      pthread_mutex_lock(L_mutex);
      removeItem(message.interconnect_name, message.path_id);
      pthread_mutex_unlock(L_mutex);
    }
  }

  // Time to exit
#ifdef DEBUG_MESSAGE
  printf("Ephemeral port manager: time to exit\n");
#endif
  return NULL;
}

static void ephemeralPortManagerFinalize(void) {
  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("Finalizing the ephemeral port manager\n");
  }

  // Wake up the thread
#ifdef _WIN32
  pthread_mutex_lock(L_mutex);
  L_thread_running = false;
  pthread_mutex_unlock(L_mutex);
#else
  // Wake up thread by signalling the pipe
  if (!pipeWakeUpSelect(L_read_pipe_fd, L_write_pipe_fd, L_error_message)) {
    // Failed to read the message
    fprintf(stderr, "Failed to use pipe to wake up ephemeral port manager thread: %s\n", L_error_message);
    exit(EXIT_FAILURE);
  }
#endif

  // Wake for thread to exit
  if (pthread_join(L_thread_id, NULL) != 0) {
    fprintf(stderr, "Failed to wait for the ephemeral port manager thread to exit\n");
    exit(EXIT_FAILURE);
  }

  // Free all pipes
#ifdef _WIN32
  // Nothing to do
#else
  pipeDestroy(L_read_pipe_fd, L_write_pipe_fd);
#endif

  // Close the sockets
  socketClose(L_multicast_recv_socket);
  socketClose(L_multicast_send_socket);

  // Free the manager items
  free(L_manager_items);
  L_manager_items = NULL;

  // Free the mutex
  pthread_mutex_destroy(L_mutex);
  free(L_mutex);
  L_mutex = NULL;

  // Free the cond var
  pthread_cond_destroy(L_cond);
  free(L_cond);
  L_cond = NULL;

  L_num_manager_items = 0;
  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("Done finalizing the ephemeral port manager\n");
  }
}

static void ephemeralPortManagerInitOnce(void) {
  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("Initializing the ephemeral port manager\n");
  }

  // Get the defaults for setting up multicasting
  const char *multicast_ip    = TAKYON_MULTICAST_IP;
  uint16_t multicast_port     = TAKYON_MULTICAST_PORT;
  const char *multicast_group = TAKYON_MULTICAST_GROUP;
  int multicast_ttl           = TAKYON_MULTICAST_TTL;

  // Check to see if any of the defualts have been overridden
  const char *user_defined_multicast_ip = getenv("TAKYON_MULTICAST_IP");
  if (user_defined_multicast_ip != NULL) {
    multicast_ip = user_defined_multicast_ip;
  }
  const char *user_defined_multicast_port = getenv("TAKYON_MULTICAST_PORT");
  if (user_defined_multicast_port != NULL) {
    uint16_t temp_value;
    int count = sscanf(user_defined_multicast_port, "%hu", &temp_value);
    if (count == 1 && temp_value >= 1024 && temp_value <= 65535) {
      multicast_port = temp_value;
    } else {
      fprintf(stderr, "Warning: ignoring env TAKYON_MULTICAST_PORT='%s', this is not an unsigned short integer between 1024 and 65535 inclusive\n", user_defined_multicast_port);
    }
  }
  const char *user_defined_multicast_group = getenv("TAKYON_MULTICAST_GROUP");
  if (user_defined_multicast_group != NULL) {
    multicast_group = user_defined_multicast_group;
  }
  const char *user_defined_multicast_ttl = getenv("TAKYON_MULTICAST_TTL");
  if (user_defined_multicast_ttl != NULL) {
    uint16_t temp_value;
    int count = sscanf(user_defined_multicast_ttl, "%hu", &temp_value);
    if (count == 1 && temp_value <= 255) {
      multicast_ttl = temp_value;
    } else {
      fprintf(stderr, "Warning: ignoring env TAKYON_MULTICAST_TTL='%s', this is not an unsigned integer between 0 and 255\n", user_defined_multicast_ttl);
    }
  }

  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("  TAKYON_MULTICAST_IP    = %s\n", multicast_ip);
    printf("  TAKYON_MULTICAST_PORT  = %hu\n", multicast_port);
    printf("  TAKYON_MULTICAST_GROUP = %s\n", multicast_group);
    printf("  TAKYON_MULTICAST_TTL   = %d\n", multicast_ttl);
  }

  // Start the resource allocations
  L_num_manager_items = 1;
  L_manager_items = calloc(L_num_manager_items, sizeof(EphemeralPortManagerItem));
  if (L_manager_items == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }

  L_mutex = calloc(1, sizeof(pthread_mutex_t));
  if (L_mutex == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  pthread_mutex_init(L_mutex, NULL);

  L_cond = calloc(1, sizeof(pthread_cond_t));
  if (L_cond == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  pthread_cond_init(L_cond, NULL);

  // Create the multicast sender socket
  L_error_message[0] = '\0';
  bool disable_loopback = false;
  if (!socketCreateMulticastSender(multicast_ip, multicast_group, multicast_port, disable_loopback, multicast_ttl, &L_multicast_send_socket, &L_multicast_send_socket_in_addr, L_error_message)) {
    fprintf(stderr, "Failed to create multicast sender for managing ephemeral port numbers: %s\n", L_error_message);
    exit(EXIT_FAILURE);
  }

  // Create the multicast receiver socket
  bool allow_reuse = true; // Need to allow other processes in the same OS to use the same port number
  if (!socketCreateMulticastReceiver(multicast_ip, multicast_group, multicast_port, allow_reuse, &L_multicast_recv_socket, L_error_message)) {
    fprintf(stderr, "Failed to create multicast receiver for managing ephemeral port numbers: %s\n", L_error_message);
    exit(EXIT_FAILURE);
  }

#ifdef _WIN32
  L_thread_running = true;
#else
  // Create pipe needed to shut down monitoring thread
  if (!pipeCreate(&L_read_pipe_fd, &L_write_pipe_fd, L_error_message)) {
    fprintf(stderr, "Failed to create pipe needed to manage the life of the ephemeral port manager thread: %s\n", L_error_message);
    exit(EXIT_FAILURE);
  }
#endif

  // Start multicast recv monitoring thread
  // IMPORTANT: the thread now owns L_error_message
  if (pthread_create(&L_thread_id, NULL, ephemeralPortMonitoringThread, NULL) != 0) {
    fprintf(stderr, "Failed to start the ephemeral port number monitoring thread\n");
    exit(EXIT_FAILURE);
  }

  // This will get called if the app calls exit() or if main does a normal return.
  if (atexit(ephemeralPortManagerFinalize) == -1) {
    fprintf(stderr, "Failed to setup atexit() in the ephemeral port manager initializer\n");
    exit(EXIT_FAILURE);
  }
}

static void ephemeralPortManagerInit(uint64_t verbosity) {
  L_verbosity = verbosity;
  // Call this to make sure the ephemeral port manager is ready to coordinate: This can be called multiple times, but it's guaranteed to atomically run only the first time called.
  if (pthread_once(&L_once_control, ephemeralPortManagerInitOnce) != 0) {
    fprintf(stderr, "Failed to start the ephemeral port manager\n");
    exit(EXIT_FAILURE);
  }
}

uint16_t ephemeralPortManagerGet(const char *interconnect_name, uint32_t path_id, int64_t timeout_ns, bool *timed_out_ret, uint64_t verbosity, char *error_message) {
  // Call this to make sure the ephemeral port number manager is ready to coordinate: This can be called multiple times, but it's guaranteed to atomically run only the first time called.
  ephemeralPortManagerInit(verbosity);

  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("Ephemeral port manager: asking for port for (interconnect='%s', path_id=%u)\n", interconnect_name, path_id);
  }

  int64_t start_time = clockTimeNanoseconds();

  // Keep trying until the timeout period occurs
  uint16_t ephemeral_port_number = 0;
  pthread_mutex_lock(L_mutex);
  while (!hasItem(interconnect_name, path_id, &ephemeral_port_number)) {
    // Get time remaining
    // NOTE: Don't want cond wait to wait the full timeout (e.g. wait forever) to allow for periodic heartbeats to multcast a request for the ephemeral port number
    int64_t cond_wait_timeout = COND_WAIT_TIMEOUT_NS;
    if (timeout_ns >= 0) {
      int64_t ellapsed_time_ns = clockTimeNanoseconds() - start_time;
      if (ellapsed_time_ns >= timeout_ns) {
        // Timed out
        pthread_mutex_unlock(L_mutex);
        if (timed_out_ret != NULL) *timed_out_ret = true;
        return 0;
      }
      int64_t remaining_timeout = timeout_ns - ellapsed_time_ns;
      if (cond_wait_timeout > remaining_timeout) {
        cond_wait_timeout = remaining_timeout;
      }
    }

    // Send muticast message to ask for port number
    {
#ifdef DEBUG_MESSAGE
      printf("Ephemeral port manager: asking again for port for (interconnect='%s', path_id=%u)\n", interconnect_name, path_id);
#endif
      EphemeralPortMessage message;
      message.command = REQUEST_EPHEMERAL_PORT;
      message.is_big_endian = endianIsBig();
      message.ephemeral_port_number = 0;
      message.path_id = path_id;
      strncpy(message.interconnect_name, interconnect_name, TAKYON_MAX_INTERCONNECT_CHARS-1);
      message.interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS-1] = '\0';
      bool is_polling = false;
      int64_t heartbeat_timeout_ns = REQUEST_TIMEOUT_NS; // Need some time to get the message out, so ignore the path's timeout period
      bool timed_out = false;
      // NOTE: this send is within a mutex, but this port manager is not used once a coonection is made and will not perturb communication performance
      if (!socketDatagramSend(L_multicast_send_socket, L_multicast_send_socket_in_addr, &message, sizeof(EphemeralPortMessage), is_polling, heartbeat_timeout_ns, &timed_out, L_error_message)) {
#ifdef WARNING_MESSAGE
        fprintf(stderr, "Warning: Ephemeral port manager: Failed to re-multicast ephemeral port info: %s. Will eventually retry.\n", L_error_message);
#endif
      }
      // NOTE: if timed out, just ignore since a subsequent attempt will be done if needed
    }

    // Sleep while waiting for data
    bool timed_out;
    bool suceeded = threadCondWait(L_mutex, L_cond, cond_wait_timeout, &timed_out, error_message);
    if (!suceeded) {
      pthread_mutex_unlock(L_mutex);
      TAKYON_RECORD_ERROR(error_message, "Failed to wait for ephemeral port number\n");
      return 0;
    }
    // NOTE: ignore if this cond wait timed out, since the begining of the while loop will check
  }

  // Remove from the database so if it's stale, it can be later replaced with the correct one
  removeItem(interconnect_name, path_id);

  pthread_mutex_unlock(L_mutex);

  if (L_verbosity & TAKYON_VERBOSITY_CREATE_DESTROY_MORE) {
    printf("Ephemeral port manager: got port for (interconnect='%s', path_id=%u) ephemeral_port_number=%hu\n", interconnect_name, path_id, ephemeral_port_number);
  }

  return ephemeral_port_number;
}

void ephemeralPortManagerRemove(const char *interconnect_name, uint32_t path_id, uint16_t ephemeral_port_number) {
  pthread_mutex_lock(L_mutex);

  EphemeralPortMessage message;
  message.command = EPHEMERAL_PORT_CONNECTED;
  message.is_big_endian = endianIsBig();
  message.ephemeral_port_number = ephemeral_port_number;
  message.path_id = path_id;
  strncpy(message.interconnect_name, interconnect_name, TAKYON_MAX_INTERCONNECT_CHARS-1);
  message.interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS-1] = '\0';
  bool is_polling = false;
  int64_t heartbeat_timeout_ns = REQUEST_TIMEOUT_NS; // Need some time to get the message out, so ignore the path's timeout period
  // NOTE: this send is within a mutex, but this port manager is not used once a coonection is made and will not perturb communication performance
  bool timed_out = false;
  if (!socketDatagramSend(L_multicast_send_socket, L_multicast_send_socket_in_addr, &message, sizeof(EphemeralPortMessage), is_polling, heartbeat_timeout_ns, &timed_out, L_error_message)) {
#ifdef WARNING_MESSAGE
    fprintf(stderr, "Warning: Ephemeral port manager: Failed to multicast EPHEMERAL_PORT_CONNECTED command: %s\n", L_error_message);
#endif
    // Should be benign since the entry in the database will eventually expire
  }

  pthread_mutex_unlock(L_mutex);
}

void ephemeralPortManagerSet(const char *interconnect_name, uint32_t path_id, uint16_t ephemeral_port_number, uint64_t verbosity) {
  // Call this to make sure the ephemeral port number manager is ready to coordinate: This can be called multiple times, but it's guaranteed to atomically run only the first time called.
  ephemeralPortManagerInit(verbosity);

  pthread_mutex_lock(L_mutex);

  // IMPORTANT: Need to guarantee that it gets into this local database in case the multicast message gets dropped
  EphemeralPortMessage message;
  message.command = NEW_EPHEMERAL_PORT;
  message.is_big_endian = endianIsBig();
  message.ephemeral_port_number = ephemeral_port_number;
  message.path_id = path_id;
  strncpy(message.interconnect_name, interconnect_name, TAKYON_MAX_INTERCONNECT_CHARS-1);
  message.interconnect_name[TAKYON_MAX_INTERCONNECT_CHARS-1] = '\0';
  bool is_polling = false;
  int64_t heartbeat_timeout_ns = REQUEST_TIMEOUT_NS; // Need some time to get the message out
  // NOTE: this send is within a mutex, but this port manager is not used once a coonection is made and will not perturb communication performance
  bool timed_out = false;
  if (!socketDatagramSend(L_multicast_send_socket, L_multicast_send_socket_in_addr, &message, sizeof(EphemeralPortMessage), is_polling, heartbeat_timeout_ns, &timed_out, L_error_message)) {
#ifdef WARNING_MESSAGE
    fprintf(stderr, "Warning: Ephemeral port manager: Failed to multicast EPHEMERAL_PORT_CONNECTED command: %s\n", L_error_message);
#endif
    // Not a big deal since the port is in the database and a remote request will eventually get it to broadcast
  }

  pthread_mutex_unlock(L_mutex);
}
