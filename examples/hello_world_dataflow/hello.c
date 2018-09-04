// This file contains the core algorithm

#include "takyon_utils.h"
#include "hello.h"

void helloTask(TakyonDataflow *dataflow, ThreadDesc *thread_desc) {
  PathDesc *path_desc = &dataflow->path_list[0];
  TakyonPath *path = (thread_desc->id == path_desc->thread_idA) ? path_desc->pathA : path_desc->pathB;
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
