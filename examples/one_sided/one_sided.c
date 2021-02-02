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

#ifdef VXWORKS_7
static int one_sided_run(int argc, char **argv) {
  bool     is_polling      = false;
  bool     is_endpointA    = false;
  int      nbufs           = 1;
  int      cycles          = 5;
  uint64_t bytes           = 1024; // Must be greater than 0

  if (argc < 2) {
    printf("Usage: one_sided(\"<interconnect_spec>\",<number_of_parameters>,\"[options]\")\n");
    printf("  Options:\n");
    printf("    -endpointA  Set the endpoint to A (default is B)\n");
    printf("    -poll       Enable polling communication (default is event driven)\n");
    printf("    -bytes <N>  The number of bytes to transfer (default is %lld)\n", (long long)bytes);
    printf("    -cycles <N> The number of times to transfer (default is %d)\n", cycles);
    printf("  Example:\n");
    printf("    one_sided(\"OneSidedSocket -client -IP=127.0.0.1 -port=12345\",3,\"-endpointA\",\"-bytes\",\"2048\")\n");
    return 1;
  }
#else
int main(int argc, char **argv) {
  bool     is_polling      = false;
  bool     is_endpointA    = false;
  int      nbufs           = 1;
  int      cycles          = 5;
  uint64_t bytes           = 1024; // Must be greater than 0

  if (argc < 2) {
    printf("Usage: one_sided(\"<interconnect_spec>\",<number_of_parameters>,\"[options]\")\n");
    printf("  Options:\n");
    printf("    -endpointA  Set the endpoint to A (default is B)\n");
    printf("    -poll       Enable polling communication (default is event driven)\n");
    printf("    -bytes <N>  The number of bytes to transfer (default is %lld)\n", (long long)bytes);
    printf("    -cycles <N> The number of times to transfer (default is %d)\n", cycles);
    return 1;
  }
#endif

  // Check command line args
  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-poll") == 0) {
      is_polling = true;
    } else if (strcmp(argv[index], "-bytes") == 0) {
      index++;
      bytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-cycles") == 0) {
      index++;
      cycles = atoi(argv[index]);
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      is_endpointA = true;
    }
    index++;
  }

  const char *interconnect = argv[1];

  // Print some startup info
  printf("Attributes:\n");
  printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
  printf("  interconnect:    \"%s\"\n", interconnect);
  printf("  mode:            %s\n", is_polling ? "polling" : "event driven");
  printf("  bytes:           %lld\n", (long long)bytes);
  printf("  cycles:          %d\n", cycles);
  printf("  nbufs:           %d\n", nbufs);

  // Create connection
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, is_polling, nbufs, nbufs, bytes, TAKYON_WAIT_FOREVER, interconnect);
  TakyonPath *path = takyonCreate(&attrs);
  takyonFreeAttributes(attrs);

  // Do transfers: this would need to be customized to the protocol of the remote endpoint
  int buffer = 0;
  for (int i=0; i<cycles; i++) {
    uint64_t bytes_to_recv = bytes; // IMPORTANT: Since this is one sided, must tell takyonRecv() how many bytes to receive
    uint64_t data_offset = 0;       // IMPORTANT: Since this is one sided, must tell takyonRecv() where to place the data in the transport buffer
    if (is_endpointA) {
      takyonSend(path, buffer, bytes, 0, 0, NULL);
      takyonRecv(path, buffer, &bytes_to_recv, &data_offset, NULL);
    } else {
      takyonRecv(path, buffer, &bytes_to_recv, &data_offset, NULL);
      takyonSend(path, buffer, bytes, 0, 0, NULL);
    }
    buffer = (buffer + 1) % nbufs;
  }
  printf("Completed %d round trip transfers of %lld bytes\n", cycles, (long long)bytes);

  // Cleanup
  takyonDestroy(&path);

  return 0;
}

#ifdef VXWORKS_7
#define ARGV_LIST_MAX 20
int one_sided(char *interconnect_spec_arg, int count, ...) {
  char *argv_list[ARGV_LIST_MAX];
  int arg_count = 0;
  argv_list[0] = "one_sided";
  arg_count++;
  if (NULL != interconnect_spec_arg) {
    argv_list[arg_count] = interconnect_spec_arg;
    arg_count++;
  }
  va_list valist;
  va_start(valist, count);
  
  if (count > (ARGV_LIST_MAX-arg_count)) {
    printf("ERROR: exceeded <number_of_parameters>\n");
    one_sided_run(1,NULL);
  }
  for(int i=0; i<count; i++) {
    argv_list[arg_count] = va_arg(valist, char*);
    arg_count++;
  }
  va_end(valist);

  return (one_sided_run(arg_count,argv_list));
}
#endif

