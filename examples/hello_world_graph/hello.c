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

#include "takyon_extensions.h"
#include "hello.h"

void helloTask(TakyonGraph *graph, TakyonThread *thread_info) {
  TakyonConnection *connection = &graph->path_list[0];
  TakyonPath *path = (thread_info->group_id == connection->group_idA) ? connection->pathA : connection->pathB;
  bool is_endpointA = path->attrs.is_endpointA;
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
}
