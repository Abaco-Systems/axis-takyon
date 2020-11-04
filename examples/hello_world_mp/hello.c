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
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: hello <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -endpointA        This process is marked as endpoint A (default is endpoint B)\n");
    return 1;
  }
  const char *interconnect = argv[1];
  int is_endpointA = (argc > 2) && (strcmp(argv[2], "-endpointA") == 0);

  TakyonPathAttributes attrs;
  attrs.is_endpointA                = is_endpointA;
  attrs.is_polling                  = false;
  attrs.abort_on_failure            = true;
  attrs.verbosity                   = TAKYON_VERBOSITY_ERRORS;
  strncpy(attrs.interconnect, interconnect, TAKYON_MAX_INTERCONNECT_CHARS-1);
  attrs.path_create_timeout         = TAKYON_WAIT_FOREVER;
  attrs.send_start_timeout          = TAKYON_WAIT_FOREVER;
  attrs.send_finish_timeout         = TAKYON_WAIT_FOREVER;
  attrs.recv_start_timeout          = TAKYON_WAIT_FOREVER;
  attrs.recv_finish_timeout         = TAKYON_WAIT_FOREVER;
  attrs.path_destroy_timeout        = TAKYON_WAIT_FOREVER;
  attrs.send_completion_method      = TAKYON_BLOCKING;
  attrs.recv_completion_method      = TAKYON_BLOCKING;
  attrs.nbufs_AtoB                  = 1;
  attrs.nbufs_BtoA                  = 1;
  uint64_t sender_max_bytes_list[1] = { 1024 };
  attrs.sender_max_bytes_list       = sender_max_bytes_list;
  uint64_t recver_max_bytes_list[1] = { 1024 };
  attrs.recver_max_bytes_list       = recver_max_bytes_list;
  size_t sender_addr_list[1]        = { 0 };
  attrs.sender_addr_list            = sender_addr_list;
  size_t recver_addr_list[1]        = { 0 };
  attrs.recver_addr_list            = recver_addr_list;

  TakyonPath *path = takyonCreate(&attrs);

  const char *message = is_endpointA ? "Hello from endpoint A" : "Hello from endpoint B";
  for (int i=0; i<5; i++) {
    if (is_endpointA) {
      strncpy((char *)path->attrs.sender_addr_list[0], message, path->attrs.sender_max_bytes_list[0]);
      takyonSend(path, 0, strlen(message)+1, 0, 0, NULL);
      takyonRecv(path, 0, NULL, NULL, NULL);
      printf("Endpoint A received message %d: %s\n", i, (char *)path->attrs.recver_addr_list[0]);
    } else {
      takyonRecv(path, 0, NULL, NULL, NULL);
      printf("Endpoint B received message %d: %s\n", i, (char *)path->attrs.recver_addr_list[0]);
      strncpy((char *)path->attrs.sender_addr_list[0], message, path->attrs.sender_max_bytes_list[0]);
      takyonSend(path, 0, strlen(message)+1, 0, 0, NULL);
    }
  }

  takyonDestroy(&path);

  return 0;
}
