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

#ifndef _takyon_utils_h_
#define _takyon_utils_h_

#include "takyon.h"

#define TAKYON_MAX_MMAP_NAME_CHARS 31                      // This small value of 31 is imposed by Apple's OSX
typedef struct _TakyonMmapHandle *TakyonMmapHandle;

//----------------------------------------------------------------
// Collective structures
//----------------------------------------------------------------
typedef struct {
  int npaths;
  TakyonPath **path_list;
} ScatterSrc;

typedef struct {
  int npaths;
  int path_index;
  TakyonPath *path;
} ScatterDest;

typedef struct {
  int npaths;
  int path_index;
  TakyonPath *path;
} GatherSrc;

typedef struct {
  int npaths;
  TakyonPath **path_list;
} GatherDest;

//----------------------------------------------------------------
// The following data structures are use to define dataflow when a
// Takyon application is initially started
//----------------------------------------------------------------
typedef struct {
  char *name;
  int thread_count;
  int *thread_id_list;
} TaskDesc;

typedef struct {
  int id;
  pthread_t thread_handle;
} ThreadDesc;

typedef struct {
  char *name;
  char *where;
  uint64_t bytes;
  void *addr;
  void *user_data;
} MemoryBlockDesc;

typedef struct {
  int id;
  int thread_count;
  ThreadDesc *thread_list;
  int memory_block_count;
  MemoryBlockDesc *memory_block_list;
} ProcessDesc;

typedef struct {
  int id;
  int thread_idA;
  int thread_idB;
  TakyonPathAttributes attrsA;
  TakyonPathAttributes attrsB;
  TakyonPath *pathA;
  TakyonPath *pathB;
} PathDesc;

typedef enum {
  COLLECTIVE_SCATTER,
  COLLECTIVE_GATHER,
} CollectiveType;

typedef struct {
  int path_id; // Can lookup thread IDs from this path
  bool src_is_endpointA;
} CollectivePath;

typedef struct {
  char *name;
  CollectiveType type;
  int num_paths;
  CollectivePath *path_list;
} CollectiveDesc;

typedef struct {
  char *app_name;
  int task_count;
  TaskDesc *task_list;
  int process_count;
  ProcessDesc *process_list;
  int path_count;
  PathDesc *path_list;
  int collective_count;
  CollectiveDesc *collective_list;
} TakyonDataflow;

//----------------------------------------------------------------
// Utility functions
//----------------------------------------------------------------

#ifdef __cplusplus
extern "C"
{
#endif

// Time
extern void takyonSleep(double dseconds);
extern double takyonTime();

// Endian
extern bool takyonEndianIsBig();
extern void takyonEndianSwapUInt16(uint16_t *data, uint64_t num_elements);
extern void takyonEndianSwapUInt32(uint32_t *data, uint64_t num_elements);
extern void takyonEndianSwapUInt64(uint64_t *data, uint64_t num_elements);

// Setting path attributes
extern TakyonPathAttributes takyonAllocAttributes(bool is_endpointA, bool is_polling, int nbufs_AtoB, int nbufs_BtoA, uint64_t bytes, double timeout, const char *interconnect);
extern void takyonFreeAttributes(TakyonPathAttributes attrs);

// Memory maps (sharing memory across processes)
extern void takyonMmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, TakyonMmapHandle *mmap_handle_ret);
extern void takyonMmapFree(TakyonMmapHandle mmap_handle);

// Collective functions
// Scatter
extern ScatterSrc *takyonScatterSrcInit(int npaths, TakyonPath **path_list);
extern ScatterDest *takyonScatterDestInit(int npaths, int path_index, TakyonPath *path);
extern void takyonScatterSend(ScatterSrc *group, int buffer, uint64_t *nbytes_list, uint64_t *soffset_list, uint64_t *doffset_list);
extern void takyonScatterRecv(ScatterDest *group, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret);
extern void takyonScatterSrcFinalize(ScatterSrc *group);
extern void takyonScatterDestFinalize(ScatterDest *group);
// Gather
extern GatherSrc *takyonGatherSrcInit(int npaths, int path_index, TakyonPath *path);
extern GatherDest *takyonGatherDestInit(int npaths, TakyonPath **path_list);
extern void takyonGatherSend(GatherSrc *group, int buffer, uint64_t nbytes, uint64_t soffset, uint64_t doffset);
extern void takyonGatherRecv(GatherDest *group, int buffer, uint64_t *nbytes_list_ret, uint64_t *offset_list_ret);
extern void takyonGatherSrcFinalize(GatherSrc *group);
extern void takyonGatherDestFinalize(GatherDest *group);

// Load and access a Takyon dataflow description from a file
extern TakyonDataflow *takyonLoadDataflowDescription(int process_id, const char *filename);
extern void takyonFreeDataflowDescription(TakyonDataflow *dataflow, int process_id);
// Create Takyon paths and collective groups(call by the threads)
extern void takyonCreateDataflowPaths(TakyonDataflow *dataflow, int thread_id);
extern void takyonDestroyDataflowPaths(TakyonDataflow *dataflow, int thread_id);
// Dataflow helper functions
extern void takyonPrintDataflow(TakyonDataflow *dataflow);
extern TaskDesc *takyonGetTask(TakyonDataflow *dataflow, int thread_id);
extern int takyonGetTaskInstance(TakyonDataflow *dataflow, int thread_id);
extern ScatterSrc *takyonGetScatterSrc(TakyonDataflow *dataflow, const char *name, int thread_id);
extern ScatterDest *takyonGetScatterDest(TakyonDataflow *dataflow, const char *name, int thread_id);
extern GatherSrc *takyonGetGatherSrc(TakyonDataflow *dataflow, const char *name, int thread_id);
extern GatherDest *takyonGetGatherDest(TakyonDataflow *dataflow, const char *name, int thread_id);
// If the takyonLoadDataflowDescription(<file>) define any memory blocks, then the user application has to define
// the following 2 functions to handle memory allocations from CPU, MMAPs, GPU, IO devices, etc.
extern void *appAllocateMemory(const char *name, const char *type, uint64_t bytes, void **user_data_ret);
extern void appFreeMemory(const char *where, void *user_data, void *addr);

#ifdef __cplusplus
}
#endif

#endif
