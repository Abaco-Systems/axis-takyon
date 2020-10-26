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

#include "reduce.h"

static void vector_max(uint64_t nelements, void *a, void *b) {
  // NOTE: result must be inplace with 'a'
  int *a2 = (int *)a;
  int *b2 = (int *)b;
  for (uint64_t i=0; i<nelements; i++) {
    a2[i] = (a2[i] >= b2[i]) ? a2[i] : b2[i];
  }
}

void reduceTask(TakyonGraph *graph, int group_id, int ncycles, bool scatter_result) {
  int tree_index = takyonGetGroupInstance(graph, group_id);
  TakyonGroup *group = takyonGetGroup(graph, group_id);
  int tree_instances = group->instances;
  TakyonCollectiveBarrier *barrier = takyonGetBarrier(graph, "barrier", group_id);
  TakyonCollectiveReduce *reduce = takyonGetReduce(graph, "reduce", group_id);
  int reduce_buffer = 0;
  int barrier_buffer = 1;
  bool is_tree_root = (tree_index == 0);
  uint64_t reduce_bytes = is_tree_root ? reduce->child_path_list[0]->attrs.sender_max_bytes_list[reduce_buffer] : reduce->parent_path->attrs.sender_max_bytes_list[reduce_buffer];
  uint64_t bytes_per_elem = sizeof(int);
  uint64_t reduce_nelements = reduce_bytes/bytes_per_elem;

  // Set up data pointers for the data to be reduced
  int *data_addr;
  if (is_tree_root) {
    data_addr = (int *)malloc(reduce_bytes * bytes_per_elem);
  } else {
    data_addr = (int *)reduce->parent_path->attrs.sender_addr_list[reduce_buffer];
  }

  for (int i=0; i<ncycles; i++) {
    // Fill in the data to be reduced
    for (uint64_t i=0; i<reduce_nelements; i++) data_addr[i] = (rand() % 1000);
    // Reduce the data
    if (is_tree_root) {
      takyonReduceRoot(reduce, reduce_buffer, reduce_nelements, bytes_per_elem, vector_max, data_addr, scatter_result);
      printf("Root Thread:");
      for (uint64_t i=0; i<reduce_nelements; i++) printf(" %d", data_addr[i]);
      printf("\n");
    } else {
      takyonReduceChild(reduce, reduce_buffer, reduce_nelements, bytes_per_elem, vector_max, scatter_result);
      if (scatter_result) {
        int *result = (int *)reduce->parent_path->attrs.recver_addr_list[reduce_buffer];
        if (tree_index == (tree_instances-1)) {
          printf("Last Thread:");
          for (uint64_t i=0; i<reduce_nelements; i++) printf(" %d", result[i]);
          printf("\n");
        }
      }
    }
    // Now do a barrier to make sure all threads are ready for another reduce
    takyonBarrierRun(barrier, barrier_buffer);
  }

  takyonBarrierFinalize(barrier);
  takyonReduceFinalize(reduce);
  if (is_tree_root) {
    free(data_addr);
  }
}
