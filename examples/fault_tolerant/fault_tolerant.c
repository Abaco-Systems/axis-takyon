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

#include "takyon.h"
#include "takyon_extensions.h"

typedef struct {
  int connection_failures;
  int send_timeouts;
  int send_failures;
  int recv_timeouts;
  int recv_failures;
  int completed_transfers;
} LifeCycleStats;

#define BUFFER_BYTES (4000000+sizeof(LifeCycleStats))
#define SECONDS_BETWEEN_PRINTS 1.0

static bool     L_is_endpointA      = false;
static char    *L_interconnect      = NULL;
static bool     L_is_multi_threaded = false;
static bool     L_is_polling        = false;
static bool     L_print_errors      = false;
static double   L_timeout           = 3.0;
static bool     L_simulate_delays   = false;

static void randomSleep(double timeout_seconds) {
  double scale_factor = (rand()%101) / 100.0; // between 0 and 100 %
  double sleep_seconds = timeout_seconds * scale_factor; // Small chance will exceed timeout period
  takyonSleep(sleep_seconds);
}

static void getArgValues(int argc, char **argv) {
  L_interconnect = argv[1];

  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-mt") == 0) {
      L_is_multi_threaded = true;
    } else if (strcmp(argv[index], "-poll") == 0) {
      L_is_polling = true;
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      L_is_endpointA = true;
    } else if (strcmp(argv[index], "-errors") == 0) {
      L_print_errors = true;
    } else if (strcmp(argv[index], "-simulateDelays") == 0) {
      L_simulate_delays = true;
    } else if (strcmp(argv[index], "-timeout") == 0) {
      index++;
      L_timeout = atof(argv[index]);
    }
    index++;
  }
}

static LifeCycleStats initLifeCycleStats() {
  LifeCycleStats stats;
  stats.connection_failures = 0;
  stats.send_timeouts = 0;
  stats.send_failures = 0;
  stats.recv_timeouts = 0;
  stats.recv_failures = 0;
  stats.completed_transfers = 0;
  return stats;
}

static void printLifeCycleDetails(int is_endpointA, LifeCycleStats *my_stats, LifeCycleStats *remote_stats) {
  printf("Endpoint %s:        connect-fails:%d, send-timeouts:%d, send-fails:%d, recv-timeouts:%d, recv-fails:%d, completed-xfers:%d\n",
         is_endpointA ? "A" : "B",
         my_stats->connection_failures, my_stats->send_timeouts, my_stats->send_failures, my_stats->recv_timeouts, my_stats->recv_failures, my_stats->completed_transfers);
  if (remote_stats != NULL) {
    printf("Endpoint %s remote: connect-fails:%d, send-timeouts:%d, send-fails:%d, recv-timeouts:%d, recv-fails:%d, completed-xfers:%d\n",
           is_endpointA ? "A" : "B",
           remote_stats->connection_failures, remote_stats->send_timeouts, remote_stats->send_failures, remote_stats->recv_timeouts, remote_stats->recv_failures, remote_stats->completed_transfers);
  }
}

static void printTransferDetails(int is_endpointA, LifeCycleStats *my_stats) {
  static double time_since_last_printed = 0;
  double current_time = takyonTime();
  bool time_to_print = (current_time - time_since_last_printed) > SECONDS_BETWEEN_PRINTS;
  if (time_to_print) {
    time_since_last_printed = current_time;
    printf("Endpoint %s: Completed transfers: %d\n", is_endpointA ? "A" : "B", my_stats->completed_transfers);
  }
}

static bool sendData(TakyonPath *path, uint64_t bytes, LifeCycleStats *stats) {
  bool timed_out;
  bool connection_ok = takyonSend(path, 0, TAKYON_SEND_FLAGS_NONE, bytes, 0, 0, &timed_out);
  if (!connection_ok || timed_out) {
    if (!connection_ok) {
      stats->send_failures++;
    }
    if (timed_out) {
      if (L_print_errors) fprintf(stderr, "Endpoint %s: Send timed out\n", path->attrs.is_endpointA ? "A" : "B");
      stats->send_timeouts++;
    }
    char *error_message = takyonDestroy(&path);
    if (error_message != NULL) {
      free(error_message);
    }
    return false;
  }
  return true;
}

static bool recvData(TakyonPath *path, LifeCycleStats *stats) {
  bool timed_out;
  bool connection_ok = takyonRecv(path, 0, TAKYON_RECV_FLAGS_NONE, NULL, NULL, &timed_out);
  if (!connection_ok || timed_out) {
    if (!connection_ok) {
      stats->recv_failures++;
    }
    if (timed_out) {
      if (L_print_errors) fprintf(stderr, "Endpoint %s: Recv timed out\n", path->attrs.is_endpointA ? "A" : "B");
      stats->recv_timeouts++;
    }
    char *error_message = takyonDestroy(&path);
    if (error_message != NULL) {
      free(error_message);
    }
    return false;
  }
  return true;
}

static void endpointTask(bool is_endpointA) {
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, L_is_polling, 1, 1, BUFFER_BYTES, L_timeout, L_interconnect);
  attrs.abort_on_failure = false;
  attrs.verbosity = L_print_errors ? TAKYON_VERBOSITY_ERRORS : TAKYON_VERBOSITY_NONE;

  LifeCycleStats my_stats = initLifeCycleStats();
  LifeCycleStats remote_stats;

  // Print some startup info
  if (!L_is_multi_threaded || is_endpointA) {
    printf("Attributes:\n");
    if (!L_is_multi_threaded) printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
    printf("  interconnect:    \"%s\"\n", L_interconnect);
    printf("  locality:        %s\n", L_is_multi_threaded ? "inter-thread" : "inter-process");
    printf("  mode:            %s\n", L_is_polling ? "polling" : "event driven");
    printf("  timeout:         %f seconds\n", L_timeout);
    printf("  simulate delays: %s\n", L_simulate_delays ? "on" : "off");
  }

  // Keep the app going on and on, even if disconnects occur
  while (1) {
    // Make a connection
    printf("Endpoint %s: Waiting for new connection\n", is_endpointA ? "A" : "B");
    TakyonPath *path = takyonCreate(&attrs);
    if (path == NULL) {
      if (attrs.error_message != NULL) { free(attrs.error_message); attrs.error_message = NULL; }
      my_stats.connection_failures++;
      continue; // Try again
    }

    // Exchange the current life cycle stats for each side
    if (is_endpointA) {
      uint64_t bytes = sizeof(my_stats);
      memcpy((void *)path->attrs.sender_addr_list[0], &my_stats, bytes);
      if (!sendData(path, bytes, &my_stats)) continue;
      if (!recvData(path, &my_stats)) continue;
      memcpy(&remote_stats, (void *)path->attrs.recver_addr_list[0], bytes);
    } else {
      uint64_t bytes = sizeof(my_stats);
      if (!recvData(path, &my_stats)) continue;
      memcpy(&remote_stats, (void *)path->attrs.recver_addr_list[0], bytes);
      memcpy((void *)path->attrs.sender_addr_list[0], &my_stats, bytes);
      if (!sendData(path, bytes, &my_stats)) continue;
    }
    printLifeCycleDetails(is_endpointA, &my_stats, L_is_multi_threaded ? NULL : &remote_stats);

    // Keep sending a round-trip data
    bool enable_random_sleeps = (L_timeout >= 0);
    while (1) {
      if (is_endpointA) {
        if (enable_random_sleeps && L_simulate_delays) randomSleep(path->attrs.send_start_timeout * 1.3);
        if (!sendData(path, BUFFER_BYTES, &my_stats)) break;
        if (enable_random_sleeps && L_simulate_delays) randomSleep(path->attrs.send_start_timeout * 3);
        if (!recvData(path, &my_stats)) break;
      } else {
        if (!recvData(path, &my_stats)) break;
        if (!sendData(path, BUFFER_BYTES, &my_stats)) break;
      }
      my_stats.completed_transfers++;
      printTransferDetails(is_endpointA, &my_stats);
    }
  }

  // Cleanup
  takyonFreeAttributes(attrs);
}

static void *endpointThread(void *user_data) {
  bool is_endpointA = (user_data != NULL);
  endpointTask(is_endpointA);
  return NULL;
}

#ifdef VXWORKS_7
static int fault_tolerant_run(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: fault_tolerant(\"<interconnect_spec>\",<number_of_parameters>,\"[options]\")\n");
    printf("  Options:\n");
    printf("    -mt             Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA      If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll           Enable polling communication (default is event driven)\n");
    printf("    -errors         Print error messages (default is no errors printed)\n");
    printf("    -timeout <N>    Set the timeout period (double value in seconds) to consider it as a\n");
    printf("                    bad connection. Use -1 for infinity. Default is %f\n", L_timeout);
    printf("    -simulateDelays Simulate delays before each transfer, where the delay period is between 0 and 3*timeout\n");
    printf("  Example:\n");
    printf("    fault_tolerant(\"InterThreadMemcpy -ID=1\",3,\"-mt\",\"-simulateDelays\",\"-poll\")\n");
    return 1;
  }
#else
int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: fault_tolerant <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -mt             Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA      If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll           Enable polling communication (default is event driven)\n");
    printf("    -errors         Print error messages (default is no errors printed)\n");
    printf("    -timeout <N>    Set the timeout period (double value in seconds) to consider it as a\n");
    printf("                    bad connection. Use -1 for infinity. Default is %f\n", L_timeout);
    printf("    -simulateDelays Simulate delays before each transfer, where the delay period is between 0 and 3*timeout\n");
    return 1;
  }
#endif

  // Parse command line args
  getArgValues(argc, argv);

  if (L_is_multi_threaded) {
    pthread_t endpointA_thread_id;
    pthread_t endpointB_thread_id;
    pthread_create(&endpointA_thread_id, NULL, endpointThread, (void *)1LL);
    pthread_create(&endpointB_thread_id, NULL, endpointThread, NULL);
    pthread_join(endpointA_thread_id, NULL);
    pthread_join(endpointB_thread_id, NULL);
  } else {
    endpointTask(L_is_endpointA);
  }

  return 0;
}

#ifdef VXWORKS_7
#define ARGV_LIST_MAX 20
int fault_tolerant(char *interconnect_spec_arg, int count, ...) {
  char *argv_list[ARGV_LIST_MAX];
  int arg_count = 0;
  argv_list[0] = "fault_tolerant";
  arg_count++;
  if (NULL != interconnect_spec_arg) {
    argv_list[arg_count] = interconnect_spec_arg;
    arg_count++;
  }
  va_list valist;
  va_start(valist, count);
  
  if (count > (ARGV_LIST_MAX-arg_count)) {
    printf("ERROR: exceeded <number_of_parameters>\n");
    return (fault_tolerant_run(1,NULL));
  }
  for (int i=0; i<count; i++) {
    argv_list[arg_count] = va_arg(valist, char*);
    arg_count++;
  }
  va_end(valist);

  return (fault_tolerant_run(arg_count,argv_list));
}
#endif
