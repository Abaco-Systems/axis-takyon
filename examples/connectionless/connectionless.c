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

static void randomSleep(double max_seconds) {
  double scale_factor = (rand()%101) / 100.0; // between 0 and 100 %
  double sleep_seconds = max_seconds * scale_factor;
  takyonSleep(sleep_seconds);
}

int main(int argc, char **argv) {
  bool     is_polling      = false;
  bool     is_endpointA    = false;
  int      nbufs           = 2;
  uint64_t datagram_bytes  = 1024; // Must be at least 4, but <= the networks datagram size
  bool     simulate_delays = false;

  if (argc < 2) {
    printf("Usage: connectionless <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -endpointA      This process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll           Enable polling communication (default is event driven)\n");
    printf("    -simulateDelays Simulate random delays before each send, to force dropped datagrams\n");
    printf("    -bytes          The number of bytes to transfer in the datagram (default is %lld)\n", (long long)datagram_bytes);
    printf("    -nbufs          The number of buffers (default is %d)\n", nbufs);
    return 1;
  }

  // Check command line args
  const char *interconnect = argv[1];
  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-poll") == 0) {
      is_polling = true;
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      is_endpointA = true;
    } else if (strcmp(argv[index], "-simulateDelays") == 0) {
      simulate_delays = true;
    } else if (strcmp(argv[index], "-bytes") == 0) {
      index++;
      datagram_bytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      nbufs = atoi(argv[index]);
    }
    index++;
  }

  // Print some startup info
  printf("Attributes:\n");
  printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
  printf("  interconnect:    \"%s\"\n", interconnect);
  printf("  mode:            %s\n", is_polling ? "polling" : "event driven");
  printf("  simulate delays: %s\n", simulate_delays ? "on" : "off");
  printf("  bytes:           %lld\n", (long long)datagram_bytes);
  printf("  nbufs:           %d\n", nbufs);

  // Create connection
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, is_polling, nbufs, 0, datagram_bytes, TAKYON_WAIT_FOREVER, interconnect);
  TakyonPath *path = takyonCreate(&attrs);
  takyonFreeAttributes(attrs);

  // Do transfers
  double time_since_last_printed = takyonTime();
  int xfer_count = 0;
  int buffer = 0;
  while (1) {
    double current_time = takyonTime();
    bool time_to_print = (current_time - time_since_last_printed) > SECONDS_BETWEEN_PRINTS;
    if (time_to_print) time_since_last_printed = current_time;
    xfer_count++;
    if (is_endpointA) {
      // Sender
      // Put the transfer count at the beginning of the message.
      int *message = (int *)path->attrs.sender_addr_list[buffer];
      message[0] = xfer_count;
      // Send the message
      takyonSend(path, buffer, datagram_bytes, 0, 0, NULL);
      if (time_to_print) printf("Sent %d messages\n", xfer_count);
    } else {
      // Sleep a random amount of time to force dropped packages
      if (simulate_delays) {
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
    buffer = (buffer + 1) % nbufs;
  }

  // Cleanup
  takyonDestroy(&path);

  return 0;
}
