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

static bool     L_test_latency      = true;
static bool     L_test_throughput   = true;
static bool     L_is_endpointA      = false;
static bool     L_validate_data     = false;
static char    *L_interconnect      = NULL;
static bool     L_is_multi_threaded = false;
static bool     L_is_polling        = false;
static int      L_nbufs             = 1;
static uint64_t L_max_bytes         = 1024*1024*4;
static uint64_t L_min_bytes         = 4;
static int      L_ncycles           = 100;
static int      L_nprime_cycles     = 1;

static void getArgValues(int argc, char **argv) {
  L_interconnect = argv[1];

  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-mt") == 0) {
      L_is_multi_threaded = true;
    } else if (strcmp(argv[index], "-lat") == 0) {
      L_test_latency    = true;
      L_test_throughput = false;
    } else if (strcmp(argv[index], "-tp") == 0) {
      L_test_latency    = false;
      L_test_throughput = true;
    } else if (strcmp(argv[index], "-poll") == 0) {
      L_is_polling = true;
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      L_is_endpointA = true;
    } else if (strcmp(argv[index], "-validate") == 0) {
      L_validate_data = true;
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      L_nbufs = atoi(argv[index]);
    } else if (strcmp(argv[index], "-max_bytes") == 0) {
      index++;
      L_max_bytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-min_bytes") == 0) {
      index++;
      L_min_bytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-ncycles") == 0) {
      index++;
      L_ncycles = atoi(argv[index]);
    } else if (strcmp(argv[index], "-nprime_cycles") == 0) {
      index++;
      L_nprime_cycles = atoi(argv[index]);
    }
    index++;
  }
}

static void fillInTestData(uint8_t *data, uint64_t bytes, int cycle) {
  for (uint64_t i=0; i<bytes; i++) {
    data[i] = (cycle+i) % 256;
  }
}

static void verifyTestData(uint8_t *data, uint64_t bytes, int cycle, uint64_t bytes_received, uint64_t offset) {
  if (bytes != bytes_received) { printf("ERROR at cycle %d: 'bytes_received' wrong, got %lld but expected %lld\n", cycle, (unsigned long long)bytes, (unsigned long long)bytes_received); exit(0); }
  if (offset != 0) { printf("ERROR at cycle %d: 'offset' wrong, got %lld but expected 0\n", cycle, (unsigned long long)offset); exit(0); }
  for (uint64_t i=0; i<bytes; i++) {
    uint8_t got = data[i];
    uint8_t expected = (cycle+i) % 256;
    if (got != expected) {
      printf("ERROR at cycle %d: data[%lld] wrong, got %lld but expected %lld\n", cycle, (unsigned long long)i, (unsigned long long)got, (unsigned long long)expected);
      exit(0);
    }
  }
}

static void testThroughput(TakyonPath *path) {
  for (uint64_t bytes=L_min_bytes; bytes<=L_max_bytes; bytes*=4) {
    if (path->attrs.is_endpointA) {
      // Endpoint A:
      //  - Initiates a one way trip on all buffers at once then waits for a single response on buffer 0
      //  - All transfer are timed as a single elapsed time, and then averaged to
      //    show the one way throughput time.
      double time1 = 0;
      for (int cycle=0; cycle<L_ncycles+L_nprime_cycles; cycle++) {
        // Send data using multiple buffers to help saturate the interconnect
        if (cycle==L_nprime_cycles) time1 = takyonTime();
        for (int buffer=0; buffer<L_nbufs; buffer++) {
          if (L_validate_data) fillInTestData((uint8_t *)path->attrs.sender_addr_list[buffer], bytes, cycle);
          takyonSend(path, buffer, bytes, 0, 0, NULL);
        }
        if (path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
          for (int buffer=0; buffer<L_nbufs; buffer++) {
            takyonSendTest(path, buffer, NULL);
          }
        }
        // Get sync signal to know all buffers are ready again
        takyonRecv(path, 0, NULL, NULL, NULL);
      }
      double elapsed_seconds = takyonTime()-time1;
      double total_Mbytes = ((double)bytes * L_ncycles * L_nbufs) / 1000000.0;
      double Mbytes_per_second = total_Mbytes / elapsed_seconds;
      printf("   %8lld bytes  %11.2f MBytes/sec\n", (unsigned long long)bytes, Mbytes_per_second);

    } else {
      // Endpoint B: Waits for messages one all buffers, then sends a single synchronization message back
      for (int cycle=0; cycle<L_ncycles+L_nprime_cycles; cycle++) {
        for (int buffer=0; buffer<L_nbufs; buffer++) {
          uint64_t bytes_received, offset;
          takyonRecv(path, buffer, &bytes_received, &offset, NULL);
          if (L_validate_data) verifyTestData((uint8_t *)path->attrs.recver_addr_list[buffer], bytes, cycle, bytes_received, offset);
        }
        // Send synchronization message (zero bytes in message)
        takyonSend(path, 0, 0, 0, 0, NULL);
      }
    }
  }
}

static void testLatency(TakyonPath *path) {
  for (uint64_t bytes=L_min_bytes; bytes<=L_max_bytes; bytes*=4) {
    int buffer = 0;
    if (path->attrs.is_endpointA) {
      // Endpoint A:
      //  - Initiates the round trip on all buffers at once then waits for a response
      //  - All transfer are timed as a single elapsed time, and then averaged to
      //    show the one way latency time.
      double time1 = 0;
      for (int cycle=0; cycle<L_ncycles+L_nprime_cycles; cycle++) {
        if (cycle==L_nprime_cycles) time1 = takyonTime();
        if (L_validate_data) fillInTestData((uint8_t *)path->attrs.sender_addr_list[buffer], bytes, cycle);
        takyonSend(path, buffer, bytes, 0, 0, NULL);
        uint64_t bytes_received, offset;
        takyonRecv(path, buffer, &bytes_received, &offset, NULL);
        if (L_validate_data) verifyTestData((uint8_t *)path->attrs.recver_addr_list[buffer], bytes, cycle, bytes_received, offset);
        buffer = (buffer+1) % L_nbufs;
      }
      double elapsed_seconds = takyonTime()-time1;
      double half_trip_seconds_per_cycle = elapsed_seconds / (L_ncycles * 2.0);
      double latency_usecs = half_trip_seconds_per_cycle*1000000.0;
      printf("  %8lld bytes  %10.2f usecs\n", (unsigned long long)bytes, latency_usecs);

    } else {
      // Endpoint B: waits for a message and then sends it right back
      for (int cycle=0; cycle<L_ncycles+L_nprime_cycles; cycle++) {
        uint64_t bytes_received, offset;
        takyonRecv(path, buffer, &bytes_received, &offset, NULL);
        if (L_validate_data) verifyTestData((uint8_t *)path->attrs.recver_addr_list[buffer], bytes, cycle, bytes_received, offset);
        if (L_validate_data) fillInTestData((uint8_t *)path->attrs.sender_addr_list[buffer], bytes, cycle);
        takyonSend(path, buffer, bytes, 0, 0, NULL);
        buffer = (buffer+1) % L_nbufs;
      }
    }
  }
}

static void endpointTask(bool is_endpointA) {
  // Attributes
  if (!L_is_multi_threaded || is_endpointA) {
    printf("Attributes:\n");
    if (!L_is_multi_threaded) printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
    printf("  tests:           %s%s%s\n", L_test_latency?"latency":"", (L_test_latency&&L_test_throughput)?", ":"", L_test_throughput?"throughput":"");
    printf("  interconnect:    \"%s\"\n", L_interconnect);
    printf("  locality:        %s\n", L_is_multi_threaded ? "inter-thread" : "inter-process");
    printf("  mode:            %s\n", L_is_polling ? "polling" : "event driven");
    printf("  nbufs:           %d\n", L_nbufs);
    printf("  max bytes:       %lld\n", (unsigned long long)L_max_bytes);
    printf("  min bytes:       %lld\n", (unsigned long long)L_min_bytes);
    printf("  cycles:          %d\n", L_ncycles);
    printf("  prime cycles:    %d\n", L_nprime_cycles);
    printf("  data validation: %s\n\n", L_validate_data ? "on" : "off");
  }

  if (!L_is_multi_threaded && !is_endpointA) {
    printf("See endpoint A for results\n");
  }

  // Latency
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, L_is_polling, L_nbufs, L_nbufs, L_max_bytes, TAKYON_WAIT_FOREVER, L_interconnect);
  TakyonPath *latency_path = takyonCreate(&attrs);
  if (L_test_latency) {
    if (is_endpointA) {
      printf("Average One-Way Latency\n");
      printf("      Block Size           Latency\n");
      printf("  --------------  ----------------\n");
    }
    testLatency(latency_path);
  }
  takyonDestroy(&latency_path);

  // Throughput
  attrs.send_completion_method = is_endpointA ? TAKYON_USE_SEND_TEST : TAKYON_BLOCKING;
  TakyonPath *throughput_path = takyonCreate(&attrs);
  takyonFreeAttributes(attrs);
  if (L_test_throughput) {
    if (is_endpointA) {
      printf("\nThroughput\n");
      printf("       Block Size              Throughput\n");
      printf("   --------------  ----------------------\n");
    }
    testThroughput(throughput_path);
  }
  takyonDestroy(&throughput_path);
}

static void *endpointThread(void *user_data) {
  bool is_endpointA = (user_data != NULL);
  endpointTask(is_endpointA);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: performance <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -mt                Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA         If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -lat               Only test latency (default is to test latency and throughput)\n");
    printf("    -tp                Only test throughput (default is to test latency and throughput)\n");
    printf("    -poll              Enable polling communication (default is event driven)\n");
    printf("    -nbufs <N>         Number of buffers. Default is %d\n", L_nbufs);
    printf("    -min_bytes <N>     Min message size in bytes. Default is %lld\n", (unsigned long long)L_min_bytes);
    printf("    -max_bytes <N>     Max message size in bytes. Default is %lld\n", (unsigned long long)L_max_bytes);
    printf("    -ncycles <N>       Number of cycles at each byte size to time. Default is %d\n", L_ncycles);
    printf("    -nprime_cycles <N> Number of cycles at start of each byte size to do before starting timer. Default is %d\n", L_nprime_cycles);
    printf("    -validate          Validate data being transferred. Default is off\n");
    return 1;
  }
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
