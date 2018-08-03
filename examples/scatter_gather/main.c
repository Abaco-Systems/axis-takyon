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

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "dataflow.h"
#include "scatter_gather.h"

#define DEFAULT_IS_POLLING      false
#define DEFAULT_NBUFS           1
#define DEFAULT_NBYTES          1024
#define DEFAULT_NCYCLES         100
#define MAX_LOCALITY_NAME_CHARS 20
#define MAX_PATHS               100

static DataflowDetails getArgValues(int argc, char **argv, int *path_index_ret) {
  DataflowDetails dataflow;
  dataflow.interconnect_list = NULL;
  dataflow.is_polling = DEFAULT_IS_POLLING;
  dataflow.npaths = 0;
  dataflow.nbufs = DEFAULT_NBUFS;
  dataflow.nbytes = DEFAULT_NBYTES;
  dataflow.ncycles = DEFAULT_NCYCLES;

  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-poll") == 0) {
      dataflow.is_polling = true;
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      dataflow.nbufs = atoi(argv[index]);
    } else if (strcmp(argv[index], "-nbytes") == 0) {
      index++;
      dataflow.nbytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-ncycles") == 0) {
      index++;
      dataflow.ncycles = atoi(argv[index]);
    } else if (strcmp(argv[index], "-pathIndex") == 0) {
      index++;
      *path_index_ret = atoi(argv[index]);
    }
    index++;
  }
  return dataflow;
}

static void loadPathList(const char *filename, DataflowDetails *dataflow) {
  // Open file
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    fprintf(stdout, "failed to open file '%s'\n", filename);
    exit(EXIT_FAILURE);
  }

  // Go to the end of the file
  if (fseek(fp, 0L, SEEK_END) == -1) {
    fclose(fp);
    printf("failed to skip to end of file\n");
    exit(EXIT_FAILURE);
  }

  // Get the file offset to know how many bytes
  long nbytes = ftell(fp);
  if (nbytes == -1) {
    fclose(fp);
    printf("failed to get file size\n");
    exit(EXIT_FAILURE);
  }

  // Go back to the beginning of the file
  if (fseek(fp, 0L, SEEK_SET) == -1) {
    fclose(fp);
    printf("failed to rewind to the beginning of fill\n");
    exit(EXIT_FAILURE);
  }

  // Allocate memory for data
  char *data = malloc(nbytes+1);
  if (data == NULL) {
    fclose(fp);
    printf("out of memory\n");
    exit(EXIT_FAILURE);
  }

  // Load the file into a string
  size_t bytes_read = fread(data, 1, nbytes, fp);
  if (bytes_read != nbytes) {
    free(data);
    fclose(fp);
    printf("Failed to load data\n");
    exit(EXIT_FAILURE);
  }
  data[bytes_read] = '\0';

  // Close the file
  if (fclose(fp) != 0) {
    free(data);
    printf("Could not close file\n");
    exit(EXIT_FAILURE);
  }

  // Determine the number of lines
  int nlines = 0;
  for (long i=0; i<nbytes; i++) {
    if (data[i] == '\n') nlines++;
  }
  if ((nlines % 3) != 0) {
    free(data);
    printf("The dataflow file does not have 3 lines per path or does not end with an empty line\n");
    exit(EXIT_FAILURE);
  }

  // Allocate the list memory
  PathDetails *list = malloc((nlines/3) * sizeof(PathDetails));
  if (list == NULL) {
    free(data);
    printf("out of memory\n");
    exit(EXIT_FAILURE);
  }

  // Fill in the list
  int line_number = 1;
  int line_chars = 0;
  int path_index = 0;
  int mode = 0;
  for (long i=0; i<nbytes; i++) {
    if (data[i] == '\n') {
      if (mode == 0) {
        // Get the locality
        char locality[MAX_LOCALITY_NAME_CHARS];
        for (int j=0; j<line_chars; j++) {
          locality[j] = data[i-line_chars+j];
        }
        locality[line_chars] = '\0';
        list[path_index].is_inter_thread = false;
        if (strcmp(locality, "Inter-Thread") == 0) {
          list[path_index].is_inter_thread = true;
        } else if (strcmp(locality, "Inter-Process") == 0) {
          list[path_index].is_inter_thread = false;
        } else {
          free(data);
          free(list);
          printf("Line %d must have either 'Inter-Thread' or 'Inter-Process'\n", line_number);
          exit(EXIT_FAILURE);
        }
        mode = 1;
        line_number++;
        line_chars = 0;

      } else if (mode == 1) {
        // Get interconnect for endpoint A
        for (int j=2; j<line_chars; j++) {
          list[path_index].interconnectA[j-2] = data[i-line_chars+j];
        }
        list[path_index].interconnectA[line_chars-2] = '\0';
        mode = 2;
        line_number++;
        line_chars = 0;

      } else if (mode == 2) {
        // Get interconnect for endpoint B
        for (int j=2; j<line_chars; j++) {
          list[path_index].interconnectB[j-2] = data[i-line_chars+j];
        }
        list[path_index].interconnectB[line_chars-2] = '\0';
        path_index++;
        mode = 0;
        line_number++;
        line_chars = 0;
      }
    } else {
      line_chars++;
    }
  }

  // Clean up
  free(data);

  dataflow->npaths = path_index;
  dataflow->interconnect_list = list;
}

int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: scatter_gather <dataflow-filename> [options]\n");
    printf("     This executable must be run one for the master and once for each\n");
    printf("     multi-process slave. Do not run for any inter-thread slaves, since\n");
    printf("     the master will start the needed threads.\n");
    printf("  <dataflow-filename>:\n");
    printf("     Each 3 lines in the file defines a path.\n");
    printf("     Line 1 must be either 'Inter-Thread' or 'Inter-Thread'\n");
    printf("     Line 2 must be '  <endpoint A interconect spec>'\n");
    printf("     Line 3 must be '  <endpoint B interconect spec>'\n");
    printf("     There must be 2 spaces before the interconnect specs\n");
    printf("     See the file 'dataflow_test1.txt' as an example\n");
    printf("  Generic Options:\n");
    printf("    -poll             Enable polling communication (default is %s)\n", DEFAULT_IS_POLLING ? "polling" : "event driven");
    printf("    -nbufs <N>        Number of buffers. Default is %d\n", DEFAULT_NBUFS);
    printf("    -nbytes <N>       Min message size in bytes. Default is %lld\n", (unsigned long long)DEFAULT_NBYTES);
    printf("    -ncycles <N>      Number of cycles at each byte size to time. Default is %d\n", DEFAULT_NCYCLES);
    printf("    -pathIndex <N>    Only used by the inter-process slaves. Defines the\n");
    printf("                      path index into <dataflow-filename> starting with 0.\n");
    printf("                      If this is not set, then it's considered the master\n");
    printf("                      process.\n");
    return 1;
  }
  int path_index = -1;
  DataflowDetails dataflow = getArgValues(argc, argv, &path_index);
  bool is_master = (path_index == -1);

  // Load the list of paths
  loadPathList(argv[1], &dataflow);

  // Attributes
  printf("Attributes:\n");
  if (is_master) {
    printf(" is collective master\n");
  } else {
    printf("  collective slave index:     %d\n", path_index);
  }
  printf("  mode:              %s\n", dataflow.is_polling ? "polling" : "event driven");
  printf("  npaths:            %d\n", dataflow.npaths);
  printf("  nbufs:             %d\n", dataflow.nbufs);
  printf("  nbytes:            %lld per message\n", (unsigned long long)dataflow.nbytes);
  printf("  ncycles:           %d\n", dataflow.ncycles);
  printf("  Dataflow paths:\n");
  for (int i=0; i<dataflow.npaths; i++) {
    printf("    Path %d: '%s'\n", i, dataflow.interconnect_list[i].is_inter_thread ? "inter-thread" : "inter-process");
    printf("      A: %s\n", dataflow.interconnect_list[i].interconnectA);
    printf("      A: %s\n", dataflow.interconnect_list[i].interconnectB);
  }

  if (is_master) {
    // Create master task
    pthread_t master_thread_id;
    pthread_create(&master_thread_id, NULL, masterScatterGatherFunction, &dataflow);
    // Create the inter-thread slave tasks
    if (dataflow.npaths > MAX_PATHS) { printf("Need to increase MAX_PATHS to %d\n", MAX_PATHS); exit(EXIT_FAILURE); }
    pthread_t slave_thread_ids[MAX_PATHS];
    memset(slave_thread_ids, 0, MAX_PATHS*sizeof(pthread_t));
    FunctionInfo slave_function_info_list[MAX_PATHS];
    for (int i=0; i<dataflow.npaths; i++) {
      if (dataflow.interconnect_list[i].is_inter_thread) {
        slave_function_info_list[i].path_index = i;
        slave_function_info_list[i].dataflow = &dataflow;
        pthread_create(&slave_thread_ids[i], NULL, slaveScatterGatherFunction, &slave_function_info_list[i]);
      }
    }
    // Now wait for the threads to complete
    pthread_join(master_thread_id, NULL);
    for (int i=0; i<dataflow.npaths; i++) {
      if (dataflow.interconnect_list[i].is_inter_thread) {
        pthread_join(slave_thread_ids[i], NULL);
      }
    }

  } else {
    // Multi-process slave
    FunctionInfo function_info;
    function_info.path_index = path_index;
    function_info.dataflow = &dataflow;
    (void)slaveScatterGatherFunction(&function_info);
  }

  // Free list
  free(dataflow.interconnect_list);

  printf("Completed successfully.\n");

  return 0;
}
