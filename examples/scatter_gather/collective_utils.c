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

#define USE_MEMORY_MAP_FOR_GATHER
// Gather pulls memory from multiple sources to one contiguous destination.
// This means it needs to be one large allocation used by all paths.
// This forces use to use an Mmap alloc which is the only allocation
// compatible with all current interconnect types.
/*+ this is a cheat using hidden Takyon functionality. Allocating memory maps should become a Takyon utility function. */
#ifdef USE_MEMORY_MAP_FOR_GATHER
typedef struct _MmapHandle *MmapHandle;
extern bool mmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, char *error_message);
extern bool mmapFree(MmapHandle mmap_handle, char *error_message);
#define MAX_ERROR_MESSAGE_CHARS 10000
#define MAX_MMAP_NAME_CHARS 31
static char L_error_message[MAX_ERROR_MESSAGE_CHARS];
static MmapHandle *L_mmap_handles;
#endif

TakyonPathAttributes allocPathAttributes(const char *interconnect, bool is_endpointA, int nbufs, uint64_t nbytes, bool is_polling, size_t *pre_alloced_sender_addr_list, size_t *pre_alloced_recver_addr_list) {
  uint64_t *sender_max_bytes_list = calloc(nbufs, sizeof(uint64_t));
  if (sender_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  uint64_t *recver_max_bytes_list = calloc(nbufs, sizeof(uint64_t));
  if (recver_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  size_t *sender_addr_list = calloc(nbufs, sizeof(uint64_t));
  if (sender_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  size_t *recver_addr_list = calloc(nbufs, sizeof(uint64_t));
  if (recver_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  for (int i=0; i<nbufs; i++) {
    sender_max_bytes_list[i] = nbytes;
    recver_max_bytes_list[i] = nbytes;
  }
  if (pre_alloced_sender_addr_list != NULL) {
    for (int i=0; i<nbufs; i++) {
      sender_addr_list[i] = pre_alloced_sender_addr_list[i];
    }
  }
  if (pre_alloced_recver_addr_list != NULL) {
    for (int i=0; i<nbufs; i++) {
      recver_addr_list[i] = pre_alloced_recver_addr_list[i];
    }
  }

  TakyonPathAttributes attrs;
  attrs.is_endpointA           = is_endpointA;
  attrs.is_polling             = is_polling;
  attrs.abort_on_failure       = true;
  attrs.verbosity              = TAKYON_VERBOSITY_ERRORS;
  strncpy(attrs.interconnect, interconnect, MAX_TAKYON_INTERCONNECT_CHARS);
  attrs.create_timeout         = TAKYON_WAIT_FOREVER;
  attrs.send_start_timeout     = TAKYON_WAIT_FOREVER;
  attrs.send_complete_timeout  = TAKYON_WAIT_FOREVER;
  attrs.recv_start_timeout     = TAKYON_WAIT_FOREVER;
  attrs.recv_complete_timeout  = TAKYON_WAIT_FOREVER;
  attrs.destroy_timeout        = TAKYON_WAIT_FOREVER;
  attrs.send_completion_method = TAKYON_BLOCKING;
  attrs.recv_completion_method = TAKYON_BLOCKING;
  attrs.nbufs_AtoB             = nbufs;
  attrs.nbufs_BtoA             = nbufs;
  attrs.sender_max_bytes_list  = sender_max_bytes_list;
  attrs.recver_max_bytes_list  = recver_max_bytes_list;
  attrs.sender_addr_list       = sender_addr_list;
  attrs.recver_addr_list       = recver_addr_list;
  attrs.error_message          = NULL;

  return attrs;
}

void freePathAttributes(TakyonPathAttributes attrs) {
  free(attrs.sender_max_bytes_list);
  free(attrs.recver_max_bytes_list);
  free(attrs.sender_addr_list);
  free(attrs.recver_addr_list);
}

ScatterGatherMaster scatterGatherMasterInit(int npaths, PathDetails *path_list, bool is_polling, uint64_t master_bytes, uint64_t slave_bytes, int nbufs) {
  ScatterGatherMaster group;

  // Set the attributes
  group.master_bytes = master_bytes;
  group.slave_bytes = slave_bytes;
  group.nbufs = nbufs;
  group.npaths = npaths;

  // Allocate the scatter/gather memory buffers that will be shared by the paths
  // NOTE: the slave endpoints will have a much smaller buffer
  group.scatter_addr_list = calloc(nbufs, sizeof(uint8_t *));
  if (group.scatter_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  group.gather_addr_list = calloc(nbufs, sizeof(uint8_t *));
  if (group.gather_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
#ifdef USE_MEMORY_MAP_FOR_GATHER
  L_mmap_handles = calloc(nbufs, sizeof(MmapHandle));
  if (L_mmap_handles == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
#endif
  for (int i=0; i<nbufs; i++) {
    group.scatter_addr_list[i] = malloc(master_bytes);
    if (group.scatter_addr_list[i] == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    // Since one of the path's might be using the "Mmap" inter connect, need to use alloc from a memory map to be safe.
#ifdef USE_MEMORY_MAP_FOR_GATHER
    char map_name[MAX_MMAP_NAME_CHARS];
    snprintf(map_name, MAX_MMAP_NAME_CHARS, "Gather_buf%d", i);
    if (!mmapAlloc(map_name, master_bytes, (void **)&group.gather_addr_list[i], &L_mmap_handles[i], L_error_message)) {
      printf("ERROR: failed to allocate receive side gather memory: %s\n", L_error_message);
      exit(1);
    }
#else
    group.gather_addr_list[i] = malloc(master_bytes);
    if (group.gather_addr_list[i] == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
#endif
  }

  // Create the paths
  group.paths = calloc(npaths, sizeof(TakyonPath *));
  if (group.paths == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
  for (int i=0; i<npaths; i++) {
    // Need to point to the correct pre-allocated memory location for each path
    size_t *sender_addr_list = calloc(nbufs, sizeof(size_t));
    if (sender_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    size_t *recver_addr_list = calloc(nbufs, sizeof(size_t));
    if (recver_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    for (int j=0; j<nbufs; j++) {
      sender_addr_list[j] = (size_t)group.scatter_addr_list[j];
      recver_addr_list[j] = (size_t)group.gather_addr_list[j];
    }
    // Create the path
    bool is_endpointA = true;
#ifdef USE_MEMORY_MAP_FOR_GATHER
    char interconnect2[MAX_TAKYON_INTERCONNECT_CHARS];
    strncpy(interconnect2, path_list[i].interconnectA, MAX_TAKYON_INTERCONNECT_CHARS);
    if (strncmp(path_list[i].interconnectA, "Mmap ", 5) == 0) {
      snprintf(interconnect2, MAX_TAKYON_INTERCONNECT_CHARS, "%s -app_alloced_recv_mem", path_list[i].interconnectA);
    }
    TakyonPathAttributes attrs = allocPathAttributes(interconnect2, is_endpointA, nbufs, master_bytes, is_polling, sender_addr_list, recver_addr_list);
#else
    TakyonPathAttributes attrs = allocPathAttributes(path_list[i].interconnectA, is_endpointA, nbufs, master_bytes, is_polling, sender_addr_list, recver_addr_list);
#endif
    printf("Creating %s path %d\n", path_list[i].is_inter_thread ? "inter-thread" : "inter-process", i);
    group.paths[i] = takyonCreate(&attrs);
    freePathAttributes(attrs);
    free(sender_addr_list);
    free(recver_addr_list);
  }

  return group;
}

void scatterGatherGroupFinialize(ScatterGatherMaster group) {
  for (int i=0; i<group.npaths; i++) {
    takyonDestroy(&group.paths[i]);
  }
  for (int i=0; i<group.nbufs; i++) {
    free(group.scatter_addr_list[i]);
#ifdef USE_MEMORY_MAP_FOR_GATHER
    if (!mmapFree(L_mmap_handles[i], L_error_message)) {
      printf("ERROR: failed to free receive side gather memory: %s\n", L_error_message);
      exit(1);
    }
#else
    free(group.gather_addr_list[i]);
#endif
  }
#ifdef USE_MEMORY_MAP_FOR_GATHER
  free(L_mmap_handles);
#endif
  free(group.scatter_addr_list);
  free(group.gather_addr_list);
  free(group.paths);
}

void scatterSend(ScatterGatherMaster group, int buffer, uint64_t nbytes, uint64_t offset_per_slave) {
  for (int j=0; j<group.npaths; j++) {
    uint64_t src_offset = j*offset_per_slave;
    uint64_t dest_offset = 0;
    takyonSend(group.paths[j], buffer, nbytes, src_offset, dest_offset, NULL);
  }
}

void gatherRecv(ScatterGatherMaster group, int buffer, uint64_t expected_offset_per_slave) {
  for (int j=0; j<group.npaths; j++) {
    uint64_t nbytes, offset;
    takyonRecv(group.paths[j], buffer, &nbytes, &offset, NULL);
    if (nbytes != group.slave_bytes) { fprintf(stderr, "Gather received %lld bytes but expect %lld bytes\n", (long long)nbytes, (long long)group.slave_bytes); abort(); }
    uint64_t expected_offset = j*expected_offset_per_slave;
    if (offset != expected_offset) { fprintf(stderr, "Gather received offset=%lld but expected %lld\n", (long long)offset, (long long)expected_offset); abort(); }
  }
}

ScatterGatherSlave scatterGatherSlaveInit(const char *interconnect, bool is_polling, uint64_t nbytes, int nbufs) {
  ScatterGatherSlave group;
  group.nbytes = nbytes;
  group.nbufs = nbufs;
  bool is_endpointA = false;
#ifdef USE_MEMORY_MAP_FOR_GATHER
  char interconnect2[MAX_TAKYON_INTERCONNECT_CHARS];
  strncpy(interconnect2, interconnect, MAX_TAKYON_INTERCONNECT_CHARS);
  if (strncmp(interconnect, "Mmap ", 5) == 0) {
    snprintf(interconnect2, MAX_TAKYON_INTERCONNECT_CHARS, "%s -remote_mmap_prefix Gather_buf", interconnect);
  }
  TakyonPathAttributes attrs = allocPathAttributes(interconnect2, is_endpointA, nbufs, nbytes, is_polling, NULL, NULL);
#else
  TakyonPathAttributes attrs = allocPathAttributes(interconnect, is_endpointA, nbufs, nbytes, is_polling, NULL, NULL);
#endif
  group.path = takyonCreate(&attrs);
  freePathAttributes(attrs);
  return group;
}

void scatterGatherSlaveFinalize(ScatterGatherSlave group) {
  takyonDestroy(&group.path);
}

void scatterRecv(ScatterGatherSlave group, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret) {
  takyonRecv(group.path, buffer, nbytes_ret, offset_ret, NULL);
}

void gatherSend(ScatterGatherSlave group, int buffer, uint64_t nbytes, uint64_t src_offset, uint64_t dest_offset) {
  takyonSend(group.path, buffer, nbytes, src_offset, dest_offset, NULL);
}
