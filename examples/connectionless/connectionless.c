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

static void randomSleep(double max_seconds) {
  double scale_factor = (rand()%101) / 100.0; // between 0 and 100 %
  double sleep_seconds = max_seconds * scale_factor;
  takyonSleep(sleep_seconds);
}

#ifdef VXWORKS_7
static int connectionless_run(int argc, char **argv) {
  bool     is_endpointA       = false;
  bool     is_polling         = false;
  int      nbufs              = 2;
  uint64_t bytes              = 1024; // Must be at least 4. Max datagram size is 64KB-40B, for RDMA this will be the max MTU size which is automatically detected
  int      cycles_per_print   = 10000;
  bool     validate_data      = false;
  bool     simulate_delays    = false;

  if (argc < 2) {
    printf("Usage: connectionless(\"<interconnect_spec>\",<number_of_parameters>,\"[options]\")\n");
    printf("  Options:\n");
    printf("    -endpointA             Set as endpoint A (default is endpoint %s)\n", is_endpointA ? "A" : "B");
    printf("    -poll                  Enable polling communication (default is %s)\n", is_polling ? "polling" : "event driven");
    printf("    -bytes <N>             The number of bytes to transfer in the datagram (default is %lld)\n", (long long)bytes);
    printf("    -nbufs <N>             The number of buffers (default is %d)\n", nbufs);
    printf("    -cyclesPerPrint <N>    Number of cycles at each byte size to time. Default is %d\n", cycles_per_print);
    printf("    -validate              Validate data being transferred. Default is %s\n", validate_data ? "on" : "off");
    printf("    -simulateDelays        Simulate random delays before each recv(), to force dropped datagrams\n");
    printf("  Example:\n");
    printf("    connectionless(\"UnicastSendSocket -IP=192.168.0.44 -port=12345\",3,\"-endpointA\",\"-bytes\",\"2048\")\n");
    return 1;
  }
#else
int main(int argc, char **argv) {
  bool     is_endpointA       = false;
  bool     is_polling         = false;
  int      nbufs              = 2;
  uint64_t bytes              = 1024; // Must be at least 4. Max datagram size is 64KB-40B, for RDMA this will be the max MTU size which is automatically detected
  int      cycles_per_print   = 10000;
  bool     validate_data      = false;
  bool     simulate_delays    = false;

  if (argc < 2) {
    printf("Usage: connectionless <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -endpointA             Set as endpoint A (default is endpoint %s)\n", is_endpointA ? "A" : "B");
    printf("    -poll                  Enable polling communication (default is %s)\n", is_polling ? "polling" : "event driven");
    printf("    -bytes <N>             The number of bytes to transfer in the datagram (default is %lld)\n", (long long)bytes);
    printf("    -nbufs <N>             The number of buffers (default is %d)\n", nbufs);
    printf("    -cyclesPerPrint <N>    Number of cycles at each byte size to time. Default is %d\n", cycles_per_print);
    printf("    -validate              Validate data being transferred. Default is %s\n", validate_data ? "on" : "off");
    printf("    -simulateDelays        Simulate random delays before each recv(), to force dropped datagrams\n");
    return 1;
  }
#endif

  // Check command line args
  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-poll") == 0) {
      is_polling = true;
    } else if (strcmp(argv[index], "-simulateDelays") == 0) {
      simulate_delays = true;
    } else if (strcmp(argv[index], "-bytes") == 0) {
      index++;
      bytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      nbufs = atoi(argv[index]);
    } else if (strcmp(argv[index], "-cyclesPerPrint") == 0) {
      index++;
      cycles_per_print = atoi(argv[index]);
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      is_endpointA = true;
    } else if (strcmp(argv[index], "-validate") == 0) {
      validate_data = true;
    }
    index++;
  }

  // Print some startup info
  const char *interconnect = argv[1];
  printf("Attributes:\n");
  printf("  endpoint:             \"%s\"\n", is_endpointA ? "A" : "B");
  printf("  interconnect:         \"%s\"\n", interconnect);
  printf("  mode:                 %s\n", is_polling ? "polling" : "event driven");
  printf("  simulate recv delays: %s\n", simulate_delays ? "on" : "off");
  printf("  bytes:                %lld\n", (long long)bytes);
  printf("  nbufs:                %d\n", nbufs);
  printf("  validate data:        %s\n", validate_data ? "yes" : "no");
  printf("  FYI:                  1MB = %d bytes\n", 1024*1024);

  // Create connection
  uint64_t udp_header_bytes = 40; // RDMA requires these extra bytes to provide the datagram header only on the receiver side
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, is_polling, nbufs, 0, bytes+udp_header_bytes, TAKYON_WAIT_FOREVER, interconnect);
  TakyonPath *path = takyonCreate(&attrs);
  takyonFreeAttributes(attrs);
  bool manually_post_recvs = path->attrs.PostRecv_supported;
  bool send_is_non_blocking = path->attrs.IsSent_supported;

  // Post initial receives
  /*+ re-post recv
  if (path->attrs.recv_transfer_mode == TAKYON_RECV_WITH_NO_REPOST) {
    for (int i=0; i<nbufs; i++) {
      takyonPostRecv(path, i);
    }
  }
  */

  // Handle book keeping on non blocking sends
  bool *waiting_on_pending_send = NULL;
  if (send_is_non_blocking) {
    waiting_on_pending_send = malloc((unsigned int)nbufs);
    for (int i=0; i<nbufs; i++) {
      waiting_on_pending_send[i] = false;
    }
  }

  // Do transfers
  int xfer_count = 0;
  int buffer_index = 0;
  while (1) {
    double start_time = takyonTime();
    int expected_first_value = -1;
    int dropped_packets = 0;
    for (int i=0; i<cycles_per_print; i++) {
      if (is_endpointA) {
        // Sender
        // Check if previous send is complete
        if (send_is_non_blocking) {
          if (waiting_on_pending_send[buffer_index]) {
            takyonIsSent(path, buffer_index, NULL);
            waiting_on_pending_send[buffer_index] = false;
          }
        }
        // Put the transfer count at the beginning of the message.
        int *message = (int *)path->attrs.sender_addr_list[buffer_index];
        message[0] = xfer_count;
        if (validate_data) {
          int num_ints = (int)(bytes / sizeof(int));
          for (int j=1; j<num_ints; j++) {
            message[j] = xfer_count+j;
          }
        }
        xfer_count++;
        // Send the message
        TakyonSendFlagsMask send_flags = send_is_non_blocking ? TAKYON_SEND_FLAGS_NON_BLOCKING : TAKYON_SEND_FLAGS_NONE;
        takyonSend(path, buffer_index, send_flags, bytes, 0, 0, NULL);
        /*+ delay to later to improve RDMA performance */
        if (send_is_non_blocking) {
          waiting_on_pending_send[buffer_index] = true;
        }

      } else {
        // Receiver
        // Sleep a random amount of time to force dropped packages
        if (simulate_delays) {
          double max_seconds = 0.0001;
          randomSleep(max_seconds);
        }
        // Wait for a message
        uint64_t bytes_received;
        uint64_t offset; // IMPORTANT: When using RDMA, the offset will not be zero due to the UDP packet header
        TakyonRecvFlagsMask recv_flags = manually_post_recvs ? TAKYON_RECV_FLAGS_MANUAL_REPOST : TAKYON_RECV_FLAGS_NONE;
        takyonRecv(path, buffer_index, recv_flags, &bytes_received, &offset, NULL);
        // Validate bytes
        if (bytes_received != bytes) {
          printf("Got %ju bytes but expect %ju. Re-run with '-bytes %ju'\n", bytes_received, bytes, bytes_received);
          return 1;
        }
        // Calculate some dropped packet stats
        int *message = (int *)(path->attrs.recver_addr_list[buffer_index] + offset);
        if (i > 0) {
          dropped_packets += (message[0] - expected_first_value);
        }
        expected_first_value = message[0];
        if (validate_data) {
          int num_ints = (int)(bytes / sizeof(int));
          for (int j=1; j<num_ints; j++) {
            if (message[j] != expected_first_value+j) {
              printf("Integer value[%d] is %d, but expectd %d. Cycle = %d, Exiting\n", j, message[j], expected_first_value+j, i);
              return 1;
            }
          }
        }
        expected_first_value++;
        // Re-post recv
        if (manually_post_recvs) {
          takyonPostRecv(path, buffer_index);
        }
      }

      // Prepare for next cycle
      buffer_index = (buffer_index + 1) % nbufs;
    }

    // Print some stats
    double elapsed_time = takyonTime() - start_time;
    double total_bytes = cycles_per_print * bytes;
    double bytes_per_sec = total_bytes / elapsed_time;
    double Mbytes_per_sec = bytes_per_sec / (1024.0 * 1024.0);
    if (is_endpointA) {
      printf("Sent %d packets,  %ju bytes/packet, %7.2f MB/sec\n", cycles_per_print, bytes, Mbytes_per_sec);
    } else {
      float dropped_percent = 100.0f * dropped_packets / (float)(cycles_per_print+dropped_packets);
      printf("Recved %d packets,  %d dropped (%0.2f%%),  %ju bytes/packet, %7.2f MB/sec\n", cycles_per_print, dropped_packets, dropped_percent, bytes, Mbytes_per_sec);
    }
  }

  // Complete any pending sends
  if (send_is_non_blocking) {
    for (int i=0; i<nbufs; i++) {
      if (waiting_on_pending_send[i]) {
        takyonIsSent(path, i, NULL);
      }
    }
  }

  // Cleanup
  takyonDestroy(&path);
  if (waiting_on_pending_send != NULL) free(waiting_on_pending_send);

  return 0;
}

#ifdef VXWORKS_7
#define ARGV_LIST_MAX 20
int connectionless(char *interconnect_spec_arg, int count, ...) {
  char *argv_list[ARGV_LIST_MAX];
  int arg_count = 0;
  argv_list[0] = "connectionless";
  arg_count++;
  if (NULL != interconnect_spec_arg) {
    argv_list[arg_count] = interconnect_spec_arg;
    arg_count++;
  }
  va_list valist;
  va_start(valist, count);
  
  if (count > (ARGV_LIST_MAX-arg_count)) {
    printf("ERROR: exceeded <number_of_parameters>\n");
    return (connectionless_run(1,NULL));
  }
  for(int i=0; i<count; i++) {
    argv_list[arg_count] = va_arg(valist, char*);
    arg_count++;
  }
  va_end(valist);

  return (connectionless_run(arg_count,argv_list));
}
#endif
