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

static bool     L_is_endpointA       = false;
static bool     L_validate_data      = false;
static char    *L_interconnect       = NULL;
static bool     L_is_multi_threaded  = false;
static bool     L_is_polling         = false;
static int      L_nbufs              = 1;
static uint64_t L_nbytes             = 4;
static int      L_ncycles            = 100000;
static int      L_nprime_cycles      = 1;
static int      L_histogram_nbuckets = 20;
static int      L_usecs_per_bucket   = 5;

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
    } else if (strcmp(argv[index], "-validate") == 0) {
      L_validate_data = true;
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      L_nbufs = atoi(argv[index]);
    } else if (strcmp(argv[index], "-nbytes") == 0) {
      index++;
      L_nbytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-ncycles") == 0) {
      index++;
      L_ncycles = atoi(argv[index]);
    } else if (strcmp(argv[index], "-nprime_cycles") == 0) {
      index++;
      L_nprime_cycles = atoi(argv[index]);
    } else if (strcmp(argv[index], "-nbuckets") == 0) {
      index++;
      L_histogram_nbuckets = atoi(argv[index]);
    } else if (strcmp(argv[index], "-usecsPerBucket") == 0) {
      index++;
      L_usecs_per_bucket = atoi(argv[index]);
    }
    index++;
  }
}

static void fillInTestData(uint8_t *data, uint64_t bytes, int cycle) {
  for (uint64_t i=0; i<bytes; i++) {
    data[i] = (cycle+i) % 256;
  }
}

static void verifyTestData(uint8_t *data, uint64_t bytes, int cycle) {
  for (uint64_t i=0; i<bytes; i++) {
    uint8_t got = data[i];
    uint8_t expected = (cycle+i) % 256;
    if (got != expected) {
      printf("ERROR at cycle %d: data[%lld] wrong, got %lld but expected %lld\n", cycle, (unsigned long long)i, (unsigned long long)got, (unsigned long long)expected);
      exit(0);
    }
  }
}

static TakyonPathAttributes allocAttributes(bool is_endpointA) {
  uint64_t *sender_max_bytes_list = calloc(L_nbufs, sizeof(uint64_t));
  if (sender_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  uint64_t *recver_max_bytes_list = calloc(L_nbufs, sizeof(uint64_t));
  if (recver_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  size_t *sender_addr_list = calloc(L_nbufs, sizeof(uint64_t));
  if (sender_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  size_t *recver_addr_list = calloc(L_nbufs, sizeof(uint64_t));
  if (recver_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  for (int i=0; i<L_nbufs; i++) {
    sender_max_bytes_list[i] = L_nbytes;
    recver_max_bytes_list[i] = L_nbytes;
  }

  TakyonPathAttributes attrs;
  attrs.is_endpointA           = is_endpointA;
  attrs.is_polling             = L_is_polling;
  attrs.abort_on_failure       = true;
  attrs.verbosity              = TAKYON_VERBOSITY_ERRORS;
  strncpy(attrs.interconnect, L_interconnect, MAX_TAKYON_INTERCONNECT_CHARS);
  attrs.create_timeout         = TAKYON_WAIT_FOREVER;
  attrs.send_start_timeout     = TAKYON_WAIT_FOREVER;
  attrs.send_complete_timeout  = TAKYON_WAIT_FOREVER;
  attrs.recv_start_timeout     = TAKYON_WAIT_FOREVER;
  attrs.recv_complete_timeout  = TAKYON_WAIT_FOREVER;
  attrs.destroy_timeout        = TAKYON_WAIT_FOREVER;
  attrs.send_completion_method = TAKYON_BLOCKING;
  attrs.recv_completion_method = TAKYON_BLOCKING;
  attrs.nbufs_AtoB             = L_nbufs;
  attrs.nbufs_BtoA             = L_nbufs;
  attrs.sender_max_bytes_list  = sender_max_bytes_list;
  attrs.recver_max_bytes_list  = recver_max_bytes_list;
  attrs.sender_addr_list       = sender_addr_list;
  attrs.recver_addr_list       = recver_addr_list;
  attrs.error_message          = NULL;

  return attrs;
}

static void freeAttributes(TakyonPathAttributes attrs) {
  free(attrs.sender_max_bytes_list);
  free(attrs.recver_max_bytes_list);
  free(attrs.sender_addr_list);
  free(attrs.recver_addr_list);
}

static void endpointTask(bool is_endpointA) {
  TakyonPathAttributes attrs = allocAttributes(is_endpointA);
  TakyonPath *path = takyonCreate(&attrs);
  freeAttributes(attrs);

  // Attributes
  if (!L_is_multi_threaded || is_endpointA) {
    printf("Attributes:\n");
    if (!L_is_multi_threaded) printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
    printf("  interconnect:      \"%s\"\n", L_interconnect);
    printf("  locality:          %s\n", L_is_multi_threaded ? "inter-thread" : "inter-process");
    printf("  mode:              %s\n", L_is_polling ? "polling" : "event driven");
    printf("  nbufs:             %d\n", L_nbufs);
    printf("  nbytes:            %lld\n", (unsigned long long)L_nbytes);
    printf("  cycles:            %d\n", L_ncycles);
    printf("  prime cycles:      %d\n", L_nprime_cycles);
    printf("  data validation:   %s\n", L_validate_data ? "on" : "off");
    printf("  histogram buckets: %d\n", L_histogram_nbuckets);
    printf("  usecs per bucket:  %d\n\n", L_usecs_per_bucket);
  }

  // Build histogram table
  int buffer = 0;
  if (path->attrs.is_endpointA) {
    // Endpoint A:
    //  - Initiates the round trip on all buffers at once then waits for a response
    //  - All transfer are timed as a single elapsed time, and then averaged to
    //    show the one way latency time.
    double ave_start_time = 0;
    // Clear the histogram
    int *histogram = calloc(L_histogram_nbuckets, sizeof(int));
    if (histogram == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    // Do round trip transfers
    for (int cycle=0; cycle<L_ncycles+L_nprime_cycles; cycle++) {
      double start_time = takyonTime();
      if (cycle == L_nprime_cycles) ave_start_time = start_time;
      // Send message
      if (L_validate_data) fillInTestData((uint8_t *)path->attrs.sender_addr_list[buffer], L_nbytes, cycle);
      takyonSend(path, buffer, L_nbytes, 0, 0, NULL);
      // Wait for the data to return
      takyonRecv(path, buffer, NULL, NULL, NULL);
      if (L_validate_data) verifyTestData((uint8_t *)path->attrs.recver_addr_list[buffer], L_nbytes, cycle);
      // Record results in histogram
      if (cycle >= L_nprime_cycles) {
        double elapsed_usecs = (takyonTime() - start_time) * 1000000.0;
        double half_trip_time = elapsed_usecs / 2.0;
        int bucket_index = (int)(half_trip_time / L_usecs_per_bucket);
        if (bucket_index >= L_histogram_nbuckets) {
          bucket_index = L_histogram_nbuckets - 1;
        } else if (bucket_index < 0) {
          bucket_index = 0;
        }
        histogram[bucket_index]++;
      }
      buffer = (buffer + 1) % L_nbufs;
    }
    // Print results
    double ave_elapsed_usecs = (takyonTime() - ave_start_time) * 1000000.0;
    double ave_half_trip_time = ave_elapsed_usecs / (L_ncycles * 2.0);
    printf("One Way Transfer Histogram: (ave = %g microseconds)\n", ave_half_trip_time);
    for (int i=0; i<L_histogram_nbuckets; i++) {
      int t1 = L_usecs_per_bucket * i;
      int t2 = L_usecs_per_bucket * (i+1);
      if (i < (L_histogram_nbuckets - 1)) {
        printf("%7d - %d to %d us\n", histogram[i], t1, t2);
      } else {
        printf("%7d - greater than %d us\n", histogram[i], t1);
      }
    }
    free(histogram);

  } else {
    // Endpoint B: waits for a message and then sends it right back
    for (int cycle=0; cycle<L_ncycles+L_nprime_cycles; cycle++) {
      takyonRecv(path, buffer, NULL, NULL, NULL);
      if (L_validate_data) verifyTestData((uint8_t *)path->attrs.recver_addr_list[buffer], L_nbytes, cycle);
      if (L_validate_data) fillInTestData((uint8_t *)path->attrs.sender_addr_list[buffer], L_nbytes, cycle);
      takyonSend(path, buffer, L_nbytes, 0, 0, NULL);
      buffer = (buffer + 1) % L_nbufs;
    }
    printf("See endpoint A for results\n");
  }

  takyonDestroy(&path);
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
    printf("    -mt                 Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA          If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll               Enable polling communication (default is event driven)\n");
    printf("    -nbufs <N>          Number of buffers. Default is %d\n", L_nbufs);
    printf("    -nbytes <N>         Min message size in bytes. Default is %lld\n", (unsigned long long)L_nbytes);
    printf("    -ncycles <N>        Number of cycles at each byte size to time. Default is %d\n", L_ncycles);
    printf("    -nprime_cycles <N>  Number of cycles at start of each byte size to do before starting timer. Default is %d\n", L_nprime_cycles);
    printf("    -validate           Validate data being transferred. Default is off\n");
    printf("    -nbuckets <N>       Number of buckets in the histogram. Default is %d\n", L_histogram_nbuckets);
    printf("    -usecsPerBucket <N> Number of microseconds per bucket in the histogram. Default is %d\n", L_usecs_per_bucket);
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
