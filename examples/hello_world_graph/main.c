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

#include "hello.h"

static TakyonGraph *L_graph = NULL;

static void *thread_entry_function(void *user_data) {
  // Run a single instance of a group
  TakyonThread *thread_info = (TakyonThread *)user_data;
  // Create only the paths used in the group instance
  takyonCreateGroupPaths(L_graph, thread_info->group_id);
  // Run the processing (found in hello.c)
  helloTask(L_graph, thread_info->group_id);
  // Destroy the paths
  takyonDestroyGroupPaths(L_graph, thread_info->group_id);
  return NULL;
}

void *appAllocateMemory(const char *name, const char *where, uint64_t bytes, void **user_data_ret) {
  if (strcmp(where, "CPU") == 0) {
    // Allocate CPU memory
    *user_data_ret = NULL;
    void *addr = malloc(bytes);
    return addr;
#ifndef VXWORKS_7
  } else if (strcmp(where, "MMAP") == 0) {
    // Allocate memory that can be shared by different processes
    char map_name[TAKYON_MAX_MMAP_NAME_CHARS];
    snprintf(map_name, TAKYON_MAX_MMAP_NAME_CHARS, "%s", name);
    TakyonMmapHandle mmap_handle;
    void *addr = NULL;
    takyonMmapAlloc(map_name, bytes, &addr, &mmap_handle);
    *user_data_ret = mmap_handle;
    return addr;
#endif
  }
  return NULL;
}

void appFreeMemory(const char *where, void *user_data, void *addr) {
  if (strcmp(where, "CPU") == 0) free(addr);
#ifndef VXWORKS_7
  else if (strcmp(where, "MMAP") == 0) takyonMmapFree((TakyonMmapHandle)user_data);
#endif
}

#ifdef VXWORKS_7
int hello_graph(int process_id_arg, char * graph_filename_arg) {
  if (graph_filename_arg == NULL) {
    printf("Usage: hello_graph(<process_id>,\"<graph_filename>\")\n");
    return 1;
  }
  int process_id = process_id_arg;
  const char *filename = graph_filename_arg;
#else
int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <process_id> <graph_filename>\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  int process_id = atoi(argv[1]);
  const char *filename = argv[2];
#endif

  // Load graph and create any memory blocks
  printf("Loading graph description '%s'...\n", filename);
  L_graph = takyonLoadGraphDescription(process_id, filename);
  takyonPrintGraph(L_graph);

  // Start the threads
  for (int i=0; i<L_graph->process_list[process_id].thread_count; i++) {
    TakyonThread *thread_info = &L_graph->process_list[process_id].thread_list[i];
    pthread_create(&thread_info->thread_handle, NULL, thread_entry_function, thread_info);
  }

  // Wait for the threads to complete
  for (int i=0; i<L_graph->process_list[process_id].thread_count; i++) {
    TakyonThread *thread_info = &L_graph->process_list[process_id].thread_list[i];
    pthread_join(thread_info->thread_handle, NULL);
  }

  // Free the graph resources
  takyonFreeGraphDescription(L_graph, process_id);
  return 0;
}
