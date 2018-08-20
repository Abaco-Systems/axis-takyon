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
#include "takyon_utils.h"

#define SECONDS_BETWEEN_PRINTS 1.0

static bool     L_is_polling        = false;
static bool     L_is_endpointA      = false;
static int      L_nbufs             = 2;
static uint64_t L_datagram_bytes    = 1024; // Must be at least 4, but <= the networks datagram size
static bool     L_simulate_delays   = false;

static void randomSleep(double max_seconds) {
  double scale_factor = (rand()%101) / 100.0; // between 0 and 100 %
  double sleep_seconds = max_seconds * scale_factor;
  takyonSleep(sleep_seconds);
}

static void getArgValues(int argc, char **argv) {
  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-poll") == 0) {
      L_is_polling = true;
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      L_is_endpointA = true;
    } else if (strcmp(argv[index], "-simulateDelays") == 0) {
      L_simulate_delays = true;
    } else if (strcmp(argv[index], "-bytes") == 0) {
      index++;
      L_datagram_bytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      L_nbufs = atoi(argv[index]);
    }
    index++;
  }
}

static TakyonPathAttributes allocAttributes(bool is_endpointA, bool is_polling, int nbufs_AtoB, int nbufs_BtoA, uint64_t bytes, const char *interconnect) {
  // Allocate the appropriate lists
  uint64_t *sender_max_bytes_list = NULL;
  size_t *sender_addr_list = NULL;
  uint64_t *recver_max_bytes_list = NULL;
  size_t *recver_addr_list = NULL;
  int sender_nbufs = is_endpointA ? nbufs_AtoB : nbufs_BtoA;
  int recver_nbufs = is_endpointA ? nbufs_BtoA : nbufs_AtoB;
  if (sender_nbufs > 0) {
    sender_max_bytes_list = calloc(sender_nbufs, sizeof(uint64_t));
    if (sender_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    sender_addr_list = calloc(sender_nbufs, sizeof(uint64_t));
    if (sender_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    for (int i=0; i<sender_nbufs; i++) {
      sender_max_bytes_list[i] = bytes;
    }
  }
  if (recver_nbufs > 0) {
    recver_max_bytes_list = calloc(recver_nbufs, sizeof(uint64_t));
    if (recver_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    recver_addr_list = calloc(recver_nbufs, sizeof(uint64_t));
    if (recver_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    for (int i=0; i<recver_nbufs; i++) {
      recver_max_bytes_list[i] = bytes;
    }
  }

  TakyonPathAttributes attrs;
  attrs.is_endpointA           = is_endpointA;
  attrs.is_polling             = is_polling;
  attrs.abort_on_failure       = true;
  attrs.verbosity              = TAKYON_VERBOSITY_ERRORS;
  strncpy(attrs.interconnect, interconnect, MAX_TAKYON_INTERCONNECT_CHARS);
  attrs.create_timeout         = TAKYON_WAIT_FOREVER;
  attrs.send_start_timeout     = TAKYON_WAIT_FOREVER;
  attrs.send_complete_timeout  = TAKYON_WAIT_FOREVER;
  attrs.recv_complete_timeout  = TAKYON_WAIT_FOREVER;
  attrs.destroy_timeout        = TAKYON_WAIT_FOREVER;
  attrs.send_completion_method = TAKYON_BLOCKING;
  attrs.recv_completion_method = TAKYON_BLOCKING;
  attrs.nbufs_AtoB             = nbufs_AtoB;
  attrs.nbufs_BtoA             = nbufs_BtoA;
  attrs.sender_max_bytes_list  = sender_max_bytes_list;
  attrs.recver_max_bytes_list  = recver_max_bytes_list;
  attrs.sender_addr_list       = sender_addr_list;
  attrs.recver_addr_list       = recver_addr_list;
  attrs.error_message          = NULL;

  return attrs;
}

static void freeAttributes(TakyonPathAttributes attrs) {
  if (attrs.sender_max_bytes_list != NULL) free(attrs.sender_max_bytes_list);
  if (attrs.recver_max_bytes_list != NULL) free(attrs.recver_max_bytes_list);
  if (attrs.sender_addr_list != NULL) free(attrs.sender_addr_list);
  if (attrs.recver_addr_list != NULL) free(attrs.recver_addr_list);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: connectionless <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -endpointA      This process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll           Enable polling communication (default is event driven)\n");
    printf("    -simulateDelays Simulate random delays before each send, to force dropped datagrams\n");
    printf("    -bytes          The number of bytes to transfer in the datagram (default is %lld)\n", (long long)L_datagram_bytes);
    printf("    -nbufs          The number of buffers (default is %d)\n", L_nbufs);
    return 1;
  }
  const char *interconnect = argv[1];
  getArgValues(argc, argv);

  // Print some startup info
  printf("Attributes:\n");
  printf("  endpoint:        \"%s\"\n", L_is_endpointA ? "A" : "B");
  printf("  interconnect:    \"%s\"\n", interconnect);
  printf("  mode:            %s\n", L_is_polling ? "polling" : "event driven");
  printf("  simulate delays: %s\n", L_simulate_delays ? "on" : "off");
  printf("  bytes:           %lld\n", (long long)L_datagram_bytes);
  printf("  nbufs:           %d\n", L_nbufs);

  // Create connection
  TakyonPathAttributes attrs = allocAttributes(L_is_endpointA, L_is_polling, L_nbufs, 0, L_datagram_bytes, interconnect);
  TakyonPath *path = takyonCreate(&attrs);
  freeAttributes(attrs);

  // Do transfers
  double time_since_last_printed = takyonTime();
  int xfer_count = 0;
  int buffer = 0;
  while (1) {
    double current_time = takyonTime();
    bool time_to_print = (current_time - time_since_last_printed) > SECONDS_BETWEEN_PRINTS;
    if (time_to_print) time_since_last_printed = current_time;
    xfer_count++;
    if (L_is_endpointA) {
      // Sender
      // Put the transfer count at the beginning of the message.
      int *message = (int *)path->attrs.sender_addr_list[buffer];
      message[0] = xfer_count;
      // Send the message
      takyonSend(path, buffer, L_datagram_bytes, 0, 0, NULL);
      if (time_to_print) printf("Sent %d messages\n", xfer_count);
    } else {
      // Sleep a random amount of time to force dropped packages
      if (L_simulate_delays) {
        double max_seconds = 0.0001;
        randomSleep(max_seconds);
      }
      // Wait for a message
      takyonRecv(path, buffer, NULL, NULL, NULL);
      // Get the expected xfer count
      int *message = (int *)path->attrs.recver_addr_list[buffer];
      int expected_xfer_count = message[0];
      // Print some stats
      int dropped_count = expected_xfer_count - xfer_count;
      if (time_to_print) printf("Received %d messages, dropped %d messages\n", xfer_count, dropped_count);
    }
    buffer = (buffer + 1) % L_nbufs;
  }

  // Cleanup
  takyonDestroy(&path);

  return 0;
}
