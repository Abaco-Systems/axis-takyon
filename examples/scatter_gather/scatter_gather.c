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

#include "collective_utils.h"
#include "dataflow.h"
#include "scatter_gather.h"

void *masterScatterGatherFunction(void *user_data) {
  DataflowDetails *dataflow = (DataflowDetails *)user_data;
  bool     is_polling = dataflow->is_polling;
  int      npaths     = dataflow->npaths;
  int      nbufs      = dataflow->nbufs;
  uint64_t nbytes     = dataflow->nbytes;
  int      ncycles    = dataflow->ncycles;

  // Create the paths
  uint64_t master_bytes = nbytes*npaths;
  uint64_t slave_bytes = nbytes;
  ScatterGatherMaster group = scatterGatherMasterInit(npaths, dataflow->interconnect_list, is_polling, master_bytes, slave_bytes, nbufs);

  // Run the main processing loop
  int buffer = 0;
  for (int i=0; i<ncycles; i++) {
    // Create the data
    for (uint64_t j=0; j<master_bytes; j++) {
      group.scatter_addr_list[buffer][j] = (uint8_t)(j+ncycles);
      group.gather_addr_list[buffer][j] = 0;
    }

    // Scatter the messages to the slaves
    uint64_t offset_per_slave = slave_bytes;
    scatterSend(group, buffer, slave_bytes, offset_per_slave);

    // Gather the modifed data
    uint64_t expected_offset_per_slave = slave_bytes;
    gatherRecv(group, buffer, expected_offset_per_slave);

    // Verify the gathered data
    for (uint64_t j=0; j<master_bytes; j++) {
      uint8_t expected = group.scatter_addr_list[buffer][j] + 1;
      if (group.gather_addr_list[buffer][j] != expected) {
        fprintf(stderr, "Gather received data[%lld]=%d but expect the value %d\n", (long long)j, group.gather_addr_list[buffer][j], expected);
        abort();
      }
    }

    // Go to the next buffer
    buffer = (buffer+1) % group.nbufs;
  }
  printf("Master completed %d cycles\n", ncycles);

  // Destroy the paths
  scatterGatherGroupFinialize(group);

  return NULL;
}

void *slaveScatterGatherFunction(void *user_data) {
  FunctionInfo *function_info = (FunctionInfo *)user_data;
  int path_index = function_info->path_index;
  DataflowDetails *dataflow = function_info->dataflow;
  bool     is_polling = dataflow->is_polling;
  int      nbufs      = dataflow->nbufs;
  uint64_t nbytes     = dataflow->nbytes;
  int      ncycles    = dataflow->ncycles;

  // Create the path
  ScatterGatherSlave group = scatterGatherSlaveInit(dataflow->interconnect_list[path_index].interconnectB, is_polling, nbytes, nbufs);

  // Run the main processing loop
  int buffer = 0;
  for (int i=0; i<ncycles; i++) {
    // Receive the data
    uint64_t nbytes, offset;
    scatterRecv(group, buffer, &nbytes, &offset);

    // Modify the data
    uint8_t *recv_data_ptr = (uint8_t *)(group.path->attrs.recver_addr_list[buffer] + offset);
    uint8_t *send_data_ptr = (uint8_t *)(group.path->attrs.sender_addr_list[buffer] + offset);
    for (uint64_t j=0; j<nbytes; j++) { send_data_ptr[j] = recv_data_ptr[j] + 1; }

    // Send back the modified data
    uint64_t dest_offset = path_index * nbytes;
    gatherSend(group, buffer, nbytes, offset, dest_offset);

    // Go to the next buffer
    buffer = (buffer+1) % nbufs;
  }
  printf("Slave %d completed %d cycles\n", path_index, ncycles);

  // Destroy the path
  scatterGatherSlaveFinalize(group);

  return NULL;
}
