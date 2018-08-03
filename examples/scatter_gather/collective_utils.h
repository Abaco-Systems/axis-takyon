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

#ifndef _collective_utils_h_
#define _collective_utils_h_

#include "takyon.h"

typedef struct {
  uint64_t master_bytes;
  uint64_t slave_bytes;
  int nbufs;
  int npaths;
  uint8_t **scatter_addr_list;
  uint8_t **gather_addr_list;
  TakyonPath **paths;
} ScatterGatherMaster;

typedef struct {
  uint64_t nbytes;
  int nbufs;
  TakyonPath *path;
} ScatterGatherSlave;

typedef struct {
  bool is_inter_thread;
  char interconnectA[MAX_TAKYON_INTERCONNECT_CHARS];
  char interconnectB[MAX_TAKYON_INTERCONNECT_CHARS];
} PathDetails;

// For allocating Takyon paths
extern TakyonPathAttributes allocPathAttributes(const char *interconnect_name, bool is_endpointA, int nbufs, uint64_t nbytes, bool is_polling, size_t *pre_alloced_sender_addr_list, size_t *pre_alloced_recver_addr_list);
extern void freePathAttributes(TakyonPathAttributes attrs);

// For the master side of a scatter/gather
extern ScatterGatherMaster scatterGatherMasterInit(int npaths, PathDetails *path_list, bool is_polling, uint64_t master_bytes, uint64_t slave_bytes, int nbufs);
extern void scatterGatherGroupFinialize(ScatterGatherMaster group);
extern void scatterSend(ScatterGatherMaster group, int buffer, uint64_t nbytes, uint64_t offset_per_slave);
extern void gatherRecv(ScatterGatherMaster group, int buffer, uint64_t expected_offset_per_slave);

// For the slave side of a scatter/gather
extern ScatterGatherSlave scatterGatherSlaveInit(const char *interconnect, bool is_polling, uint64_t nbytes, int nbufs);
extern void scatterGatherSlaveFinalize(ScatterGatherSlave group);
extern void scatterRecv(ScatterGatherSlave group, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret);
extern void gatherSend(ScatterGatherSlave group, int buffer, uint64_t nbytes, uint64_t src_offset, uint64_t dest_offset);

#endif
