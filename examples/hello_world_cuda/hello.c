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
#include <cuda_runtime.h>

#define MAX_BUFFER_NAME 10
#define NUM_LOOPS 3

// IMPORTANT: this will be set by main.c if the graph file name contains the word "shared", which shloud only be used if the interconnect uses a shared pointer.
bool G_interconnect_is_shared_pointer = false;

const char *endpointMemoryName(TakyonGraph *graph, bool for_endpointA, int buffer_index) {
  // Check if memory buffer is pre-allocated by graph file
  TakyonProcess *process_list = for_endpointA ? &graph->process_list[0] : &graph->process_list[graph->process_count-1];
  char expected_buffer_name[MAX_BUFFER_NAME];
  sprintf(expected_buffer_name, "buf_%s%d", for_endpointA ? "A" : "B", buffer_index);
  for (int i=0; i<process_list->buffer_count; i++) {
    TakyonBuffer *buffer = &process_list->buffer_list[i];
    if (strcmp(buffer->name, expected_buffer_name) == 0) {
      // Found the buffer
      return buffer->where;
    }
  }

  // Not a preallocated buffer, so look at the interconnect args
  const char *interconnect = for_endpointA ? graph->path_list[0].attrsA.interconnect : graph->path_list[0].attrsB.interconnect;
  const char *expected_arg = for_endpointA ? "-srcCudaDeviceId=" : "-destCudaDeviceId=";
  const char *arg = strstr(interconnect, expected_arg);
  if (arg != NULL) return "CUDA-Takyon-managed";
  return "CPU-Takyon-managed";
}

void helloTask(TakyonGraph *graph, int group_id) {
  TakyonConnection *connection = &graph->path_list[0];
  TakyonPath *path = (group_id == connection->group_idA) ? connection->pathA : connection->pathB;
  bool is_endpointA = path->attrs.is_endpointA;
  int nbufs_AtoB = path->attrs.nbufs_AtoB;
  uint64_t max_bytes = is_endpointA ? path->attrs.sender_max_bytes_list[0] : path->attrs.recver_max_bytes_list[0];
  char *expected_message = (char *)malloc(max_bytes);
  char *received_message = (char *)malloc(max_bytes);

  for (int loop_index=0; loop_index<NUM_LOOPS; loop_index++) {
    // Send messages on all buffers
    for (int buffer_index=0; buffer_index<nbufs_AtoB; buffer_index++) {
      // Determine if the src and dest memory buffers are CPU or CUDA
      const char *endpointA_mem_name = endpointMemoryName(graph, true, buffer_index);
      const char *endpointB_mem_name = endpointMemoryName(graph, false, buffer_index);
      bool endpointA_is_cuda = (strncmp(endpointA_mem_name, "CUDA", 4) == 0);
      bool endpointB_is_cuda = (strncmp(endpointB_mem_name, "CUDA", 4) == 0);

      // If this is a shared pointeer interconnect, then only check the dest buffer
      if (G_interconnect_is_shared_pointer) {
        endpointA_mem_name = endpointB_mem_name;
        endpointA_is_cuda = endpointB_is_cuda;
      }

      // Build the expected message in CPU memory
      sprintf(expected_message, "(loop %d) Hello from endpoint A's buffer %d (%s -> %s)", loop_index+1, buffer_index, endpointA_mem_name, endpointB_mem_name);
      uint64_t msg_bytes = strlen(expected_message)+1;

      // Do the message transfers
      if (is_endpointA) {
        // Move the message to the transport buffer
        void *src_addr = (void *)path->attrs.sender_addr_list[buffer_index];
        if (endpointA_is_cuda) {
          cudaError_t cuda_status = cudaMemcpy(src_addr, expected_message, msg_bytes, cudaMemcpyHostToDevice);
          if (cuda_status != cudaSuccess) { fprintf(stderr, "cudaMemcpy(send_data) Failed\n"); exit(EXIT_FAILURE); }
        } else {
          strncpy(src_addr, expected_message, msg_bytes);
        }
        // Send the message
        if (graph->process_count > 1) printf("Endpoint 'A' sending message: \"%s\"\n", expected_message);
        takyonSend(path, buffer_index, msg_bytes, 0, 0, NULL);

      } else {
        // Wait for the message to arrive
        uint64_t recved_bytes;
        takyonRecv(path, buffer_index, &recved_bytes, NULL, NULL);
        // Verify the correct number of bytes was received
        if (recved_bytes != msg_bytes) { fprintf(stderr, "Failed to get expected message bytes: got %ju but expected %ju\n", recved_bytes, msg_bytes); exit(EXIT_FAILURE); }
        // Move the message to a local CPU buffer
        void *dest_addr = (char *)path->attrs.recver_addr_list[buffer_index];
        if (endpointB_is_cuda) {
          cudaError_t cuda_status = cudaMemcpy(received_message, dest_addr, msg_bytes, cudaMemcpyDeviceToHost);
          if (cuda_status != cudaSuccess) { fprintf(stderr, "cudaMemcpy(recv_data) Failed\n"); exit(EXIT_FAILURE); }
        } else {
          strncpy(received_message, dest_addr, msg_bytes);
        }
        // Verify the correct message arrived
        if (strcmp(received_message, expected_message) != 0) { fprintf(stderr, "Failed to get expected message on %s buffer %d:\nmessage = '%s'\nexpected='%s'\n", endpointB_is_cuda ? "CUDA" : "CPU", buffer_index, received_message, expected_message); exit(EXIT_FAILURE); }
        // Print out the message
        printf("(loop %d) Endpoint 'B' received correct message: \"%s\"\n", loop_index+1, received_message);
      }
    }

    // Send sync message from B to A to avoid issues with sending subsequent messages from A to B
    if (is_endpointA) {
      takyonRecv(path, 0, NULL, NULL, NULL);
      if (graph->process_count > 1) printf("\n");
    } else {
      takyonSend(path, 0, 0, 0, 0, NULL);
      printf("\n");
    }
  }

  free(expected_message);
  free(received_message);
}
