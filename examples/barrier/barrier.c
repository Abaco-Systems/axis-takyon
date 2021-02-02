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

#include "barrier.h"

void barrierTask(TakyonGraph *graph, int group_id, int ncycles) {
  TakyonCollectiveBarrier *barrier = takyonGetBarrier(graph, "barrier", group_id);
  int barrier_buffer = 0;
  int barrier_nbufs = (barrier->parent_path != NULL) ? barrier->parent_path->attrs.nbufs_AtoB : barrier->child_path_list[0]->attrs.nbufs_AtoB;

  for (int i=0; i<ncycles; i++) {
    // Run barrier
    takyonBarrierRun(barrier, barrier_buffer);

    // Go to next buffer
    barrier_buffer = (barrier_buffer + 1) % barrier_nbufs;
  }

  takyonBarrierFinalize(barrier);
}
