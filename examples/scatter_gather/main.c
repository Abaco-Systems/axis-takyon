// This file contains the setup framework (creates the dataflow and threads, but does not contain the core algorithm)

#include "takyon_utils.h"
#include "scatter_gather.h"

static int L_ncycles = 100;
static TakyonDataflow *L_dataflow = NULL;

static void *thread_entry_function(void *user_data) {
  ThreadDesc *thread_desc = (ThreadDesc *)user_data;
  TaskDesc *task_desc = takyonGetTask(L_dataflow, thread_desc->id);

  // Create Takyon paths
  takyonCreateDataflowPaths(L_dataflow, thread_desc->id);

  // Run correct thread
  if (strcmp(task_desc->name, "master")==0) {
    masterTask(L_dataflow, thread_desc, L_ncycles);
  } else if (strcmp(task_desc->name, "slaves")==0) {
    slaveTask(L_dataflow, thread_desc, L_ncycles);
  } else {
    printf("Could not find correct task to run in thread\n");
    exit(EXIT_FAILURE);
  }

  // Destroy Takyon paths
  takyonDestroyDataflowPaths(L_dataflow, thread_desc->id);
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
    printf("Usage: %s <process_id> <dataflow_filename> [options]\n", argv[0]);
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

  // Load dataflow and create any memory blocks
  int process_id = atoi(argv[1]);
  const char *filename = argv[2];
  printf("Loading dataflow description '%s'...\n", filename);
  L_dataflow = takyonLoadDataflowDescription(process_id, filename);
  takyonPrintDataflow(L_dataflow);
  if (process_id >= L_dataflow->process_count) {
    printf("ERROR: No threads defined for this process id = %d\n", process_id);
    exit(EXIT_FAILURE);
  }

  // Start the threads
  printf("Starting threads...\n");
  for (int i=0; i<L_dataflow->process_list[process_id].thread_count; i++) {
    ThreadDesc *thread_desc = &L_dataflow->process_list[process_id].thread_list[i];
    pthread_create(&thread_desc->thread_handle, NULL, thread_entry_function, thread_desc);
  }

  // Wait for the threads to complete
  for (int i=0; i<L_dataflow->process_list[process_id].thread_count; i++) {
    ThreadDesc *thread_desc = &L_dataflow->process_list[process_id].thread_list[i];
    pthread_join(thread_desc->thread_handle, NULL);
  }

  // Free the dataflow resources
  takyonFreeDataflowDescription(L_dataflow, process_id);
  printf("Completed %d scatter gather cycles successfully!\n", L_ncycles);
  return 0;
}
