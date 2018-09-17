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

// This file defines all of the Takyon open source extensions

#ifndef _takyon_extensions_h_
#define _takyon_extensions_h_

#include "takyon.h"

#define TAKYON_MAX_MMAP_NAME_CHARS 31                      // This small value of 31 is imposed by Apple's OSX
typedef struct _TakyonMmapHandle *TakyonMmapHandle;

//----------------------------------------------------------------
// Collective structures
//----------------------------------------------------------------
typedef struct {
  int nchildren;
  TakyonPath *parent_path;
  TakyonPath **child_path_list;
} TakyonCollectiveBarrier;

typedef struct {
  int npaths;                   // Total paths in the collective
  /*+*/
  int num_src_paths;
  int num_dest_paths;
  TakyonPath **src_path_list;   // NULL is not using this thread
  TakyonPath **dest_path_list;  // NULL is not using this thread
} TakyonCollectiveOne2One;

typedef struct {
  int npaths;
  TakyonPath **path_list;
} TakyonScatterSrc;

typedef struct {
  int npaths;
  int path_index;
  TakyonPath *path;
} TakyonScatterDest;

typedef struct {
  int npaths;
  int path_index;
  TakyonPath *path;
} TakyonGatherSrc;

typedef struct {
  int npaths;
  TakyonPath **path_list;
} TakyonGatherDest;

//----------------------------------------------------------------
// The following data structures are use to define a communication
// graph when a Takyon application is initially started
//----------------------------------------------------------------
typedef struct {
  char *name;
  int instances;
  int starting_thread_id;
} TakyonThreadGroup;

typedef struct {
  int id;
  pthread_t thread_handle;
} TakyonThread;

typedef struct {
  char *name;
  char *where;
  uint64_t bytes;
  void *addr;
  void *user_data;
} TakyonMemoryBlock;

typedef struct {
  int id;
  int thread_count;
  TakyonThread *thread_list;
  int memory_block_count;
  TakyonMemoryBlock *memory_block_list;
} TakyonProcess;

typedef struct {
  int id;
  int thread_idA;
  int thread_idB;
  TakyonPathAttributes attrsA;
  TakyonPathAttributes attrsB;
  TakyonPath *pathA;
  TakyonPath *pathB;
} TakyonConnection;

typedef enum {
  TAKYON_COLLECTIVE_BARRIER,
  TAKYON_COLLECTIVE_ONE2ONE,
  TAKYON_COLLECTIVE_SCATTER,
  TAKYON_COLLECTIVE_GATHER,
} TakyonCollectiveType;

typedef struct {
  int path_id; // Can lookup thread IDs from this path
  bool src_is_endpointA;
} TakyonCollectiveConnection;

typedef struct _TakyonPathTree {
  int path_index;
  int num_children;
  struct _TakyonPathTree *children;
  struct _TakyonPathTree *parent;
} TakyonPathTree;

typedef struct {
  char *name;
  TakyonCollectiveType type;
  int num_paths;
  TakyonCollectiveConnection *path_list;
  TakyonPathTree *path_tree;
} TakyonCollectiveGroup;

typedef struct {
  int thread_group_count;
  TakyonThreadGroup *thread_group_list;
  int process_count;
  TakyonProcess *process_list;
  int path_count;
  TakyonConnection *path_list;
  int collective_count;
  TakyonCollectiveGroup *collective_list;
} TakyonGraph;

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
// Barrier
extern TakyonCollectiveBarrier *takyonBarrierInit(int nchildren, TakyonPath *parent_path, TakyonPath **child_path_list);
extern void takyonBarrierRun(TakyonCollectiveBarrier *collective, int buffer);
extern void takyonBarrierFinalize(TakyonCollectiveBarrier *collective);
// One2One
extern TakyonCollectiveOne2One *takyonOne2OneInit(int npaths, int num_src_paths, int num_dest_paths, TakyonPath **src_path_list, TakyonPath **dest_path_list);
extern void takyonOne2OneFinalize(TakyonCollectiveOne2One *collective);
// Scatter
extern TakyonScatterSrc *takyonScatterSrcInit(int npaths, TakyonPath **path_list);
extern TakyonScatterDest *takyonScatterDestInit(int npaths, int path_index, TakyonPath *path);
extern void takyonScatterSend(TakyonScatterSrc *collective, int buffer, uint64_t *nbytes_list, uint64_t *soffset_list, uint64_t *doffset_list);
extern void takyonScatterRecv(TakyonScatterDest *collective, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret);
extern void takyonScatterSrcFinalize(TakyonScatterSrc *collective);
extern void takyonScatterDestFinalize(TakyonScatterDest *collective);
// Gather
extern TakyonGatherSrc *takyonGatherSrcInit(int npaths, int path_index, TakyonPath *path);
extern TakyonGatherDest *takyonGatherDestInit(int npaths, TakyonPath **path_list);
extern void takyonGatherSend(TakyonGatherSrc *collective, int buffer, uint64_t nbytes, uint64_t soffset, uint64_t doffset);
extern void takyonGatherRecv(TakyonGatherDest *collective, int buffer, uint64_t *nbytes_list_ret, uint64_t *offset_list_ret);
extern void takyonGatherSrcFinalize(TakyonGatherSrc *collective);
extern void takyonGatherDestFinalize(TakyonGatherDest *collective);

// Load and access a Takyon graph description from a file
extern TakyonGraph *takyonLoadGraphDescription(int process_id, const char *filename);
extern void takyonFreeGraphDescription(TakyonGraph *graph, int process_id);
// Create Takyon paths and collective groups (called by the threads)
extern void takyonCreateGraphPaths(TakyonGraph *graph, int thread_id);
extern void takyonDestroyGraphPaths(TakyonGraph *graph, int thread_id);
// Graph helper functions
extern void takyonPrintGraph(TakyonGraph *graph);
extern TakyonThreadGroup *takyonGetThreadGroup(TakyonGraph *graph, int thread_id);
extern int takyonGetThreadGroupInstance(TakyonGraph *graph, int thread_id);
extern TakyonCollectiveBarrier *takyonGetBarrier(TakyonGraph *graph, const char *name, int thread_id);
extern TakyonCollectiveOne2One *takyonGetOne2One(TakyonGraph *graph, const char *name, int thread_id);
extern TakyonScatterSrc *takyonGetScatterSrc(TakyonGraph *graph, const char *name, int thread_id);
extern TakyonScatterDest *takyonGetScatterDest(TakyonGraph *graph, const char *name, int thread_id);
extern TakyonGatherSrc *takyonGetGatherSrc(TakyonGraph *graph, const char *name, int thread_id);
extern TakyonGatherDest *takyonGetGatherDest(TakyonGraph *graph, const char *name, int thread_id);
// If the takyonLoadGraphDescription(<file>) is used, then the user application has to define
// the following 2 functions to handle memory allocations from CPU, MMAPs, GPU, IO devices, etc.
extern void *appAllocateMemory(const char *name, const char *where, uint64_t bytes, void **user_data_ret);
extern void appFreeMemory(const char *where, void *user_data, void *addr);

#ifdef __cplusplus
}
#endif

#endif
