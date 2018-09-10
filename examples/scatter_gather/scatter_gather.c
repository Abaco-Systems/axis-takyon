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
#include "scatter_gather.h"

void masterTask(TakyonGraph *graph, TakyonThread *thread_info, int ncycles) {
  TakyonScatterSrc *scatter_src = takyonGetScatterSrc(graph, "scatter", thread_info->id);
  TakyonGatherDest *gather_dest = takyonGetGatherDest(graph, "gather", thread_info->id);
  int buffer = 0;
  int nbufs = scatter_src->path_list[0]->attrs.nbufs_AtoB;
  int num_slaves = scatter_src->npaths;
  uint64_t master_bytes = scatter_src->path_list[0]->attrs.sender_max_bytes_list[0];
  uint64_t slave_bytes = master_bytes / num_slaves;
  uint64_t *nbytes_list = (uint64_t *)malloc(num_slaves*sizeof(uint64_t));
  uint64_t *soffset_list = (uint64_t *)malloc(num_slaves*sizeof(uint64_t));
  uint64_t *doffset_list = (uint64_t *)malloc(num_slaves*sizeof(uint64_t));

  for (int i=0; i<ncycles; i++) {
    // Fill the data
    uint8_t *send_addr = (uint8_t *)scatter_src->path_list[0]->attrs.sender_addr_list[buffer];
    uint8_t *recv_addr = (uint8_t *)gather_dest->path_list[0]->attrs.recver_addr_list[buffer];
    for (uint64_t j=0; j<master_bytes; j++) {
      send_addr[j] = (uint8_t)(j+ncycles);
      recv_addr[j] = 0;
    }

    // Send the data
    for (int i=0; i<num_slaves; i++) {
      nbytes_list[i] = slave_bytes;
      soffset_list[i] = i*slave_bytes;
      doffset_list[i] = 0;
    }
    takyonScatterSend(scatter_src, buffer, nbytes_list, soffset_list, doffset_list);

    // Wait for the modified data to arrive
    takyonGatherRecv(gather_dest, buffer, nbytes_list, doffset_list);

    // Verify the received data
    for (uint64_t j=0; j<master_bytes; j++) {
      uint8_t expected = send_addr[j] + 1;
      if (recv_addr[j] != expected) {
        fprintf(stderr, "Gather received data[%lld]=%d but expect the value %d\n", (long long)j, recv_addr[j], expected);
        exit(EXIT_FAILURE);
      }
    }

    buffer = (buffer + 1) % nbufs;
  }

  takyonScatterSrcFinalize(scatter_src);
  takyonGatherDestFinalize(gather_dest);
  free(nbytes_list);
  free(soffset_list);
  free(doffset_list);
}

void slaveTask(TakyonGraph *graph, TakyonThread *thread_info, int ncycles) {
  TakyonScatterDest *scatter_dest = takyonGetScatterDest(graph, "scatter", thread_info->id);
  TakyonGatherSrc *gather_src = takyonGetGatherSrc(graph, "gather", thread_info->id);
  int task_instance = takyonGetThreadGroupInstance(graph, thread_info->id);
  int buffer = 0;
  int nbufs = scatter_dest->path->attrs.nbufs_AtoB;
  uint64_t slave_bytes = scatter_dest->path->attrs.recver_max_bytes_list[0];

  for (int i=0; i<ncycles; i++) {
    // Wait for the data
    uint64_t nbytes;
    uint64_t offset;
    takyonScatterRecv(scatter_dest, buffer, &nbytes, &offset);

    // Modify the data
    uint8_t *recv_addr = (uint8_t *)scatter_dest->path->attrs.recver_addr_list[buffer];
    uint8_t *send_addr = (uint8_t *)gather_src->path->attrs.sender_addr_list[buffer];
    for (uint64_t j=0; j<nbytes; j++) { send_addr[j] = recv_addr[j] + 1; }

    // Send back the modified data
    uint64_t soffset = 0;
    uint64_t doffset = task_instance * slave_bytes;
    takyonGatherSend(gather_src, buffer, nbytes, soffset, doffset);

    buffer = (buffer + 1) % nbufs;
  }

  takyonScatterDestFinalize(scatter_dest);
  takyonGatherSrcFinalize(gather_src);
}
