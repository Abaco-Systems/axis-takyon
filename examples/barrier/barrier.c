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
#include "barrier.h"

void barrierTask(TakyonGraph *graph, int group_id, int ncycles) {
  TakyonGroup *group = takyonGetGroup(graph, group_id);
  int pipe_index = takyonGetGroupInstance(graph, group_id);
  TakyonCollectiveBarrier *barrier = takyonGetBarrier(graph, "barrier", group_id);
  TakyonCollectiveOne2One *pipeline = takyonGetOne2One(graph, "pipeline", group_id);
  TakyonPath *src_path = pipeline->src_path_list[0];
  TakyonPath *dest_path = pipeline->dest_path_list[0];
  int pipeline_buffer = 0;
  int pipeline_nbufs = src_path->attrs.nbufs_AtoB;
  int barrier_buffer = 0;
  int barrier_nbufs = (barrier->parent_path != NULL) ? barrier->parent_path->attrs.nbufs_AtoB : barrier->child_path_list[0]->attrs.nbufs_AtoB;
  uint64_t bytes = sizeof(int);
  bool is_last_in_pipe = (pipe_index == (group->instances - 1));

  for (int i=0; i<ncycles; i++) {
    int *send_addr = (int *)src_path->attrs.sender_addr_list[pipeline_buffer];
    int *recv_addr = (int *)dest_path->attrs.recver_addr_list[pipeline_buffer];
    send_addr[0] = pipe_index + i;

    // Pass pipe index around on pipe
    // NOTE: Can't have all send or else deadlock may occur with sockets: last thread in pipe does recv then send
    if (!is_last_in_pipe) {
      // Send then recv
      takyonSend(src_path, pipeline_buffer, bytes, 0, 0, NULL);
      if (src_path->attrs.send_completion_method == TAKYON_USE_IS_SEND_FINISHED) takyonIsSendFinished(src_path, pipeline_buffer, NULL);
      takyonRecv(dest_path, pipeline_buffer, NULL, NULL, NULL);
    } else {
      // Last thread in pipe: recv then send
      takyonRecv(dest_path, pipeline_buffer, NULL, NULL, NULL);
      takyonSend(src_path, pipeline_buffer, bytes, 0, 0, NULL);
      if (src_path->attrs.send_completion_method == TAKYON_USE_IS_SEND_FINISHED) takyonIsSendFinished(src_path, pipeline_buffer, NULL);
    }

    // Verify data
    int expected_pipe_index = i + (pipe_index - 1 + group->instances) % group->instances;
    if (recv_addr[0] != expected_pipe_index) {
      fprintf(stderr, "Pipe[%d] got unexpected value=%d, expected=%d at cycle %d\n", pipe_index, recv_addr[0], expected_pipe_index, i);
      exit(EXIT_FAILURE);
    }
    recv_addr[0] = -1;

    // Now do a barrier to make sure all threads are ready for new data
    takyonBarrierRun(barrier, barrier_buffer);

    // Go to next buffer
    pipeline_buffer = (pipeline_buffer + 1) % pipeline_nbufs;
    barrier_buffer = (barrier_buffer + 1) % barrier_nbufs;
  }

  takyonBarrierFinalize(barrier);
  takyonOne2OneFinalize(pipeline);
}
