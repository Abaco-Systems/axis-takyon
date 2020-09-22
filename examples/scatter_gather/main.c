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

static int L_ncycles = 100;
static TakyonGraph *L_graph = NULL;

static void *thread_entry_function(void *user_data) {
  TakyonThread *thread_info = (TakyonThread *)user_data;
  TakyonGroup *group = takyonGetGroup(L_graph, thread_info->group_id);

  // Create Takyon paths
  takyonCreateGroupPaths(L_graph, thread_info->group_id);

  // Run correct thread
  if (strcmp(group->name, "parent")==0) {
    parentTask(L_graph, thread_info, L_ncycles);
  } else if (strcmp(group->name, "child")==0) {
    childTask(L_graph, thread_info, L_ncycles);
  } else {
    printf("Could not find correct task to run in thread\n");
    exit(EXIT_FAILURE);
  }

  // Destroy Takyon paths
  takyonDestroyGroupPaths(L_graph, thread_info->group_id);
  return NULL;
}

void *appAllocateMemory(const char *name, const char *where, uint64_t bytes, void **user_data_ret) {
  if (strcmp(where, "CPU") == 0) {
    *user_data_ret = NULL;
    void *addr = malloc(bytes);
    return addr;
  } else if (strcmp(where, "MMAP") == 0) {
    char map_name[TAKYON_MAX_MMAP_NAME_CHARS];
    snprintf(map_name, TAKYON_MAX_MMAP_NAME_CHARS, "%s", name);
    TakyonMmapHandle mmap_handle;
    void *addr = NULL;
    takyonMmapAlloc(map_name, bytes, &addr, &mmap_handle);
    *user_data_ret = mmap_handle;
    return addr;
  }
  return NULL;
}

void appFreeMemory(const char *where, void *user_data, void *addr) {
  if (strcmp(where, "CPU") == 0) free(addr);
  else if (strcmp(where, "MMAP") == 0) takyonMmapFree((TakyonMmapHandle)user_data);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: %s <process_id> <graph_filename> [options]\n", argv[0]);
    printf("  Options:\n");
    printf("    -ncycles <N>       Number of cycles to process the data. Default is %d\n", L_ncycles);
    exit(EXIT_FAILURE);
  }

  // Get args
  int index = 3;
  while (index < argc) {
    if (strcmp(argv[index], "-ncycles") == 0) {
      index++;
      L_ncycles = atoi(argv[index]);
    }
    index++;
  }
  printf("ncycles = %d\n", L_ncycles);

  // Load graph and create any memory blocks
  int process_id = atoi(argv[1]);
  const char *filename = argv[2];
  printf("Loading graph description '%s'...\n", filename);
  L_graph = takyonLoadGraphDescription(process_id, filename);
  takyonPrintGraph(L_graph);
  if (process_id >= L_graph->process_count) {
    printf("ERROR: No threads defined for this process id = %d\n", process_id);
    exit(EXIT_FAILURE);
  }

  // Start the threads
  printf("Starting threads...\n");
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
  printf("Completed %d scatter gather cycles successfully!\n", L_ncycles);
  return 0;
}
