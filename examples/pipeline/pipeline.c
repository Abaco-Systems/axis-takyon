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

#include "pipeline.h"

void pipelineTask(TakyonGraph *graph, int group_id, int ncycles) {
  TakyonGroup *group = takyonGetGroup(graph, group_id);
  int pipe_index = takyonGetGroupInstance(graph, group_id);
  TakyonCollectiveOne2One *collective = takyonGetOne2One(graph, "pipeline", group_id);
  TakyonPath *src_path = (collective->num_src_paths > 0) ? NULL : collective->src_path_list[0];
  TakyonPath *dest_path = (collective->num_dest_paths > 0) ? NULL : collective->dest_path_list[0];
  if ((src_path == NULL) && (dest_path == NULL)) {
    fprintf(stderr, "Pipeline is missing the paths for thread %s[%d]\n", group->name, pipe_index);
    exit(EXIT_FAILURE);
  }
  int buffer = 0;
  int nbufs = (src_path != NULL) ? src_path->attrs.nbufs_AtoB : dest_path->attrs.nbufs_AtoB;
  uint64_t bytes = (src_path != NULL) ? src_path->attrs.sender_max_bytes_list[0] : dest_path->attrs.recver_max_bytes_list[0];

  for (int i=0; i<ncycles; i++) {
    if (pipe_index == 0) {
      // Start of the pipeline
      // Wait for sync (permission to send data)
      if (i > 0) { takyonRecv(src_path, 0, NULL, NULL, NULL); }
      // Fill the data
      uint8_t *send_addr = (uint8_t *)src_path->attrs.sender_addr_list[buffer];
      for (uint64_t j=0; j<bytes; j++) { send_addr[j] = (uint8_t)(j+ncycles); }
      // Send the data
      takyonSend(src_path, buffer, bytes, 0, 0, NULL);
      if (src_path->attrs.send_completion_method == TAKYON_USE_IS_SEND_FINISHED) takyonIsSendFinished(src_path, buffer, NULL);

    } else if (pipe_index < collective->npaths) {
      // Intermediate thread in the pipeline
      uint8_t *send_addr = (uint8_t *)src_path->attrs.sender_addr_list[buffer];
      uint8_t *recv_addr = (uint8_t *)dest_path->attrs.recver_addr_list[buffer];
      // Wait for data
      takyonRecv(dest_path, buffer, NULL, NULL, NULL);
      // Modify the data
      for (uint64_t j=0; j<bytes; j++) { send_addr[j] = recv_addr[j]+1; recv_addr[j] = 0; }
      // Send the sync to the previous thread to allow more data to be sent
      if (i < (ncycles-1)) { takyonSend(dest_path, 0, 0, 0, 0, NULL); }
      // Wait for sync (permission to send data)
      if (i > 0) { takyonRecv(src_path, 0, NULL, NULL, NULL); }
      // Send the data
      takyonSend(src_path, buffer, bytes, 0, 0, NULL);
      if (src_path->attrs.send_completion_method == TAKYON_USE_IS_SEND_FINISHED) takyonIsSendFinished(src_path, buffer, NULL);

    } else {
      // End of pipeline
      uint8_t *recv_addr = (uint8_t *)dest_path->attrs.recver_addr_list[buffer];
      // Wait for data
      takyonRecv(dest_path, buffer, NULL, NULL, NULL);
      // Verify the results
      for (uint64_t j=0; j<bytes; j++) {
        uint8_t expected = (uint8_t)(j + ncycles + (collective->npaths-1));
        if (recv_addr[j] != expected) {
          fprintf(stderr, "Pipeline data[%lld]=%d but expect the value %d\n", (long long)j, recv_addr[j], expected);
          exit(EXIT_FAILURE);
        }
        recv_addr[j] = 0;
      }
      // Send the sync to the previous thread to allow more data to be sent
      if (i < (ncycles-1)) { takyonSend(dest_path, 0, 0, 0, 0, NULL); }
    }

    buffer = (buffer + 1) % nbufs;
  }

  takyonOne2OneFinalize(collective);
}
