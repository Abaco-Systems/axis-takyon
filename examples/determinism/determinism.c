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

static bool     L_is_endpointA       = false;
static bool     L_validate_data      = false;
static char    *L_interconnect       = NULL;
static bool     L_is_multi_threaded  = false;
static bool     L_is_polling         = false;
static int      L_nbufs              = 1;
static uint64_t L_nbytes             = 4;
static int      L_ncycles            = 100000;
static int      L_nprime_cycles      = 1;
static int      L_histogram_nbuckets = 30;
static int      L_usecs_per_bucket   = 100;

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
    data[i] = (uint8_t)((cycle+i) % 256);
  }
}

static void verifyTestData(uint8_t *data, uint64_t bytes, int cycle) {
  for (uint64_t i=0; i<bytes; i++) {
    uint8_t got = data[i];
    uint8_t expected = (uint8_t)((cycle+i) % 256);
    if (got != expected) {
      printf("ERROR at cycle %d: data[%ju] wrong, got %u but expected %u\n", cycle, i, got, expected);
      exit(0);
    }
  }
}

static void endpointTask(bool is_endpointA) {
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, L_is_polling, L_nbufs, L_nbufs, L_nbytes, TAKYON_WAIT_FOREVER, L_interconnect);
  TakyonPath *path = takyonCreate(&attrs);
  takyonFreeAttributes(attrs);

  // Attributes
  if (!L_is_multi_threaded || is_endpointA) {
    printf("Attributes:\n");
    if (!L_is_multi_threaded) printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
    printf("  interconnect:      \"%s\"\n", L_interconnect);
    printf("  locality:          %s\n", L_is_multi_threaded ? "inter-thread" : "inter-process");
    printf("  mode:              %s\n", L_is_polling ? "polling" : "event driven");
    printf("  nbufs:             %d\n", L_nbufs);
    printf("  nbytes:            %ju\n", L_nbytes);
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

#ifdef VXWORKS_7
static int determinism_run(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: determinism(\"<interconnect_spec>\",<number_of_parameters>,\"[options]\")\n");
    printf("  Options:\n");
    printf("    -mt                 Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA          If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll               Enable polling communication (default is event driven)\n");
    printf("    -nbufs <N>          Number of buffers. Default is %d\n", L_nbufs);
    printf("    -nbytes <N>         Min message size in bytes. Default is %ju\n", L_nbytes);
    printf("    -ncycles <N>        Number of cycles at each byte size to time. Default is %d\n", L_ncycles);
    printf("    -nprime_cycles <N>  Number of cycles at start of each byte size to do before starting timer. Default is %d\n", L_nprime_cycles);
    printf("    -validate           Validate data being transferred. Default is off\n");
    printf("    -nbuckets <N>       Number of buckets in the histogram. Default is %d\n", L_histogram_nbuckets);
    printf("    -usecsPerBucket <N> Number of microseconds per bucket in the histogram. Default is %d\n", L_usecs_per_bucket);
    printf("  Example:\n");
    printf("    determinism(\"InterThreadMemcpy -ID=1\",2,\"-mt\",\"-poll\")\n");
    return 1;
  }
#else
int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: performance <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -mt                 Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA          If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll               Enable polling communication (default is event driven)\n");
    printf("    -nbufs <N>          Number of buffers. Default is %d\n", L_nbufs);
    printf("    -nbytes <N>         Min message size in bytes. Default is %ju\n", L_nbytes);
    printf("    -ncycles <N>        Number of cycles at each byte size to time. Default is %d\n", L_ncycles);
    printf("    -nprime_cycles <N>  Number of cycles at start of each byte size to do before starting timer. Default is %d\n", L_nprime_cycles);
    printf("    -validate           Validate data being transferred. Default is off\n");
    printf("    -nbuckets <N>       Number of buckets in the histogram. Default is %d\n", L_histogram_nbuckets);
    printf("    -usecsPerBucket <N> Number of microseconds per bucket in the histogram. Default is %d\n", L_usecs_per_bucket);
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
int determinism(char *interconnect_spec_arg, int count, ...) {
  char *argv_list[ARGV_LIST_MAX];
  int arg_count = 0;
  argv_list[0] = "determinism";
  arg_count++;
  if (NULL != interconnect_spec_arg) {
    argv_list[arg_count] = interconnect_spec_arg;
    arg_count++;
  }
  va_list valist;
  va_start(valist, count);
  
  if (count > (ARGV_LIST_MAX-arg_count)) {
    printf("ERROR: exceeded <number_of_parameters>\n");
    return (determinism_run(1,NULL));
  }
  for(int i=0; i<count; i++) {
    argv_list[arg_count] = va_arg(valist, char*);
    arg_count++;
  }
  va_end(valist);

  return (determinism_run(arg_count,argv_list));
}
#endif
