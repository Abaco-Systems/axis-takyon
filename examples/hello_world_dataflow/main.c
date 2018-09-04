// This file contains the setup framework (creates the dataflow and threads, but does not contain the core algorithm)

#include "takyon_utils.h"
#include "hello.h"

static TakyonDataflow *L_dataflow = NULL;

static void *thread_entry_function(void *user_data) {
  ThreadDesc *thread_desc = (ThreadDesc *)user_data;
  takyonCreateDataflowPaths(L_dataflow, thread_desc->id);
  helloTask(L_dataflow, thread_desc);
  takyonDestroyDataflowPaths(L_dataflow, thread_desc->id);
  return NULL;
}

void *appAllocateMemory(const char *name, const char *where, uint64_t bytes, void **user_data_ret) {
  if (strcmp(where, "CPU") == 0) return malloc(bytes);
  return NULL;
}

void appFreeMemory(const char *where, void *user_data, void *addr) {
  if (strcmp(where, "CPU") == 0) free(addr);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <process_id> <dataflow_filename>\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  // Load dataflow and create any memory blocks
  int process_id = atoi(argv[1]);
  const char *filename = argv[2];
  printf("Loading dataflow description '%s'...\n", filename);
  L_dataflow = takyonLoadDataflowDescription(process_id, filename);
  takyonPrintDataflow(L_dataflow);
  // Start the threads
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
  return 0;
}
