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
#ifdef _WIN32
#define strdup _strdup
#endif

#define MAX_KEYWORD_BYTES 30
#define MAX_VALUE_BYTES 100000
//#define DEBUG_MESSAGE

static char *loadFile(const char *filename) {
  // Open file
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    fprintf(stderr, "%s(): Failed to open file '%s'\n", __FUNCTION__, filename);
    exit(EXIT_FAILURE);
  }

  // Go to the end of the file
  if (fseek(fp, 0L, SEEK_END) == -1) {
    fclose(fp);
    fprintf(stderr, "%s(): Failed to skip to end of file\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }

  // Get the file offset to know how many bytes are in the file
  long nbytes = ftell(fp);
  if (nbytes == -1) {
    fclose(fp);
    fprintf(stderr, "%s(): Failed to get file size\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }

  // Go back to the beginning of the file
  if (fseek(fp, 0L, SEEK_SET) == -1) {
    fclose(fp);
    fprintf(stderr, "%s(): Failed to rewind to the beginning of fill\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }

  // Allocate memory for file data
  char *data = malloc(nbytes+1);
  if (data == NULL) {
    fclose(fp);
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }

  // Load the file into a string
  size_t bytes_read = fread(data, 1, nbytes, fp);
  if (bytes_read != nbytes) {
    free(data);
    fclose(fp);
    fprintf(stderr, "%s(): Failed to load data\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  data[bytes_read] = '\0';

  // Close the file
  if (fclose(fp) != 0) {
    free(data);
    fprintf(stderr, "%s(): Could not close file\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }

  return data;
}

static bool is_space(char c) {
  if ((c == ' ') ||  // Space
      (c == '\f') || // Form feed
      (c == '\n') || // New line
      (c == '\r') || // Carriage return
      (c == '\t') || // Horizontal tab
      (c == '\v')    // Vertical tab
      ) {
    return true;
  }
  return false;
}

static bool is_endofstring(char c) {
  if (c == '\0') return true;
  return false;
}

static bool is_newline(char c) {
  if (c == '\n') return true;
  return false;
}

static char *getNextKeyValue(char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  keyword[0] = '\0';
  value[0] = '\0';

 restart:
  // Ignore white space
  while (!is_endofstring(*data_ptr) && is_space(*data_ptr)) {
    if (is_newline(*data_ptr)) *line_count_ret = *line_count_ret + 1;
    data_ptr++;
  }
  if (is_endofstring(*data_ptr)) {
    // No more tokens
    return NULL;
  }
  if (*data_ptr == '#') {
    // This is a comment, skip to end of line
    while (!is_endofstring(*data_ptr) && !is_newline(*data_ptr)) {
      data_ptr++;
    }
    goto restart;
  }

  // Found a keyword
  int char_count = 0;
  while (!is_endofstring(*data_ptr) && !is_space(*data_ptr)) {
    if (char_count == (MAX_KEYWORD_BYTES-1)) {
      fprintf(stderr, "Line %d: keyword is longer than %d characters\n", *line_count_ret, MAX_KEYWORD_BYTES);
      exit(EXIT_FAILURE);
    }
    keyword[char_count] = *data_ptr;
    char_count++;
    data_ptr++;
  }
  keyword[char_count] = '\0';

  // Skip white space (but not the end of line)
  while (!is_endofstring(*data_ptr) && !is_newline(*data_ptr) && is_space(*data_ptr)) {
    if (is_newline(*data_ptr)) *line_count_ret = *line_count_ret + 1;
    data_ptr++;
  }

  // Get value
  char_count = 0;
  while (!is_endofstring(*data_ptr) && !is_newline(*data_ptr)) {
    if (char_count == (MAX_VALUE_BYTES-1)) {
      fprintf(stderr, "Line %d: value is longer than %d characters\n", *line_count_ret, MAX_VALUE_BYTES);
      exit(EXIT_FAILURE);
    }
    value[char_count] = *data_ptr;
    char_count++;
    data_ptr++;
  }
  value[char_count] = '\0';

  // Remove white space from end of value
  char_count--;
  while ((char_count >= 0) && is_space(value[char_count])) {
    value[char_count] = '\0';
    char_count--;
  }

  return data_ptr;
}

static void validateKey(const char *expected_keyword, const char *data_ptr, const char *keyword, const int line_count) {
  if (data_ptr == NULL) {
    fprintf(stderr, "Line %d: expected keyword '%s', but at end of file\n", line_count, expected_keyword);
    exit(EXIT_FAILURE);
  }
  if (strcmp(keyword, expected_keyword) != 0) {
    fprintf(stderr, "Line %d: expected keyword '%s', but got '%s'\n", line_count, expected_keyword, keyword);
    exit(EXIT_FAILURE);
  }
}

static void validateKeyValue(const char *expected_keyword, const char *data_ptr, const char *keyword, const char *value, const int line_count) {
  validateKey(expected_keyword, data_ptr, keyword, line_count);
  if (strlen(value) == 0) {
    fprintf(stderr, "Line %d: no value for keyword '%s'\n", line_count, keyword);
    exit(EXIT_FAILURE);
  }
}

static void validateAllocation(void *addr, const char *function) {
  if (addr == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", function);
    exit(EXIT_FAILURE);
  }
}

static void validateUniqueNames(const char *name1, const char *name2, int line_count) {
  if (strcmp(name1, name2) == 0) {
    fprintf(stderr, "Line %d: Name '%s' already used\n", line_count, name1);
    exit(EXIT_FAILURE);
  }
}

static void validateUniqueInt(int n1, int n2, int line_count) {
  if (n1 == n2) {
    fprintf(stderr, "Line %d: Value '%d' already used\n", line_count, n1);
    exit(EXIT_FAILURE);
  }
}

static int getTotalThreads(TakyonGraph *graph) {
  TakyonThreadGroup *last_thread_group = &graph->thread_group_list[graph->thread_group_count-1];
  int total_threads = last_thread_group->starting_thread_id + last_thread_group->instances;
  return total_threads;
}

static TakyonCollectiveGroup *takyonGetCollective(TakyonGraph *graph, const char *name) {
  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollectiveGroup *collective_desc = &graph->collective_list[i];
    if (strcmp(collective_desc->name, name) == 0) return collective_desc;
  }
  return NULL;
}

static const char *collectiveTypeToName(TakyonCollectiveType type) {
  if (type == TAKYON_COLLECTIVE_ONE2ONE) return "ONE2ONE";
  if (type == TAKYON_COLLECTIVE_SCATTER) return "SCATTER";
  if (type == TAKYON_COLLECTIVE_GATHER) return "GATHER";
  return "Unknown";
}

TakyonThreadGroup *takyonGetThreadGroupByName(TakyonGraph *graph, const char *name) {
  for (int i=0; i<graph->thread_group_count; i++) {
    TakyonThreadGroup *thread_group_desc = &graph->thread_group_list[i];
    if (strcmp(thread_group_desc->name, name)==0) {
      return thread_group_desc;
    }
  }
  return NULL;
}

TakyonThreadGroup *takyonGetThreadGroup(TakyonGraph *graph, int thread_id) {
  for (int i=0; i<graph->thread_group_count; i++) {
    TakyonThreadGroup *thread_group_desc = &graph->thread_group_list[i];
    int ending_thread_id = thread_group_desc->starting_thread_id + (thread_group_desc->instances - 1);
    if ((thread_id >= thread_group_desc->starting_thread_id) && (thread_id <= ending_thread_id)) {
      return thread_group_desc;
    }
  }
  return NULL;
}

int takyonGetThreadGroupInstance(TakyonGraph *graph, int thread_id) {
  TakyonThreadGroup *thread_group_desc = takyonGetThreadGroup(graph, thread_id);
  if (thread_group_desc != NULL) {
    return thread_id - thread_group_desc->starting_thread_id;
  }
  return -1;
}

static TakyonProcess *getProcess(TakyonGraph *graph, int process_id) {
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    if (process_desc->id == process_id) {
      return process_desc;
    }
  }
  return NULL;
}

static TakyonMemoryBlock *getMemoryBlock(TakyonGraph *graph, const char *mem_name) {
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    for (int j=0; j<process_desc->memory_block_count; j++) {
      TakyonMemoryBlock *memory_block = &process_desc->memory_block_list[j];
      if (strcmp(memory_block->name, mem_name) == 0) {
        return memory_block;
      }
    }
  }
  return NULL;
}

static TakyonConnection *getPath(TakyonGraph *graph, int path_id) {
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    if (path_desc->id == path_id) {
      return path_desc;
    }
  }
  return NULL;
}

static bool getBoolValue(const char *data, int line_count) {
  if (strcmp(data, "true")==0) {
    return true;
  } else if (strcmp(data, "false")==0) {
    return false;
  } else {
    fprintf(stderr, "Line %d: expected 'true' or 'false', but got '%s'\n", line_count, data);
    exit(EXIT_FAILURE);
  }
  return false;
}

static int getIntValue(const char *data, int line_count, int min_value) {
  int number;
  int tokens = sscanf(data, "%d", &number);
  if ((tokens != 1) || (number < 0)) {
    fprintf(stderr, "Line %d: numbers must be greater or equal to zero. Found: %s\n", line_count, data);
    exit(EXIT_FAILURE);
  }
  return number;
}

static uint64_t getUInt64Value(const char *data, int line_count) {
  unsigned long long number;
  int tokens = sscanf(data, "%llu", &number);
  if (tokens != 1) {
    fprintf(stderr, "Line %d: numbers must be greater or equal to zero. Found: %s\n", line_count, data);
    exit(EXIT_FAILURE);
  }
  return (uint64_t)number;
}

static uint64_t getVerbosityValue(const char *data, int line_count) {
  uint64_t verbosity = 0;
  static char token[MAX_VALUE_BYTES];
  while (data[0] != '\0') {
    // Get next token
    int index = 0;
    while ((data[0] != '\0') && (!is_space(data[0]))) {
      token[index] = data[0];
      data++;
      index++;
    }
    token[index] = '\0';
    // Process token
    if (strcmp(token, "TAKYON_VERBOSITY_NONE") == 0) verbosity |= TAKYON_VERBOSITY_NONE;
    else if (strcmp(token, "TAKYON_VERBOSITY_ERRORS") == 0) verbosity |= TAKYON_VERBOSITY_ERRORS;
    else if (strcmp(token, "TAKYON_VERBOSITY_INIT") == 0) verbosity |= TAKYON_VERBOSITY_INIT;
    else if (strcmp(token, "TAKYON_VERBOSITY_INIT_DETAILS") == 0) verbosity |= TAKYON_VERBOSITY_INIT_DETAILS;
    else if (strcmp(token, "TAKYON_VERBOSITY_RUNTIME") == 0) verbosity |= TAKYON_VERBOSITY_RUNTIME;
    else if (strcmp(token, "TAKYON_VERBOSITY_RUNTIME_DETAILS") == 0) verbosity |= TAKYON_VERBOSITY_RUNTIME_DETAILS;
    else {
      fprintf(stderr, "Line %d: '%s' is not a valid Takyon verbosity name\n", line_count, token);
      exit(EXIT_FAILURE);
    }
    // Skip white space
    while ((data[0] != '\0') && is_space(data[0])) {
      data++;
    }
  }
  return verbosity;
}

static double getDoubleValue(const char *data, int line_count) {
  if (strcmp(data, "TAKYON_WAIT_FOREVER")==0) {
    return (double)TAKYON_WAIT_FOREVER;
  } else if (strcmp(data, "TAKYON_NO_WAIT")==0) {
    return (double)TAKYON_NO_WAIT;
  }
  double number;
  int tokens = sscanf(data, "%lf", &number);
  if ((tokens != 1) || (number < 0)) {
    fprintf(stderr, "Line %d: double numbers must be 'TAKYON_WAIT_FOREVER', 'TAKYON_NO_WAIT', or greater or equal to zero. Found: '%s'\n", line_count, data);
    exit(EXIT_FAILURE);
  }
  return number;
}

static TakyonCompletionMethod getCompletionMethod(const char *data, int line_count) {
  if (strcmp(data, "TAKYON_BLOCKING")==0) {
    return TAKYON_BLOCKING;
  } else if (strcmp(data, "TAKYON_USE_SEND_TEST")==0) {
    return TAKYON_USE_SEND_TEST;
  } else {
    fprintf(stderr, "Line %d: Expected 'TAKYON_BLOCKING' or 'TAKYON_USE_SEND_TEST' but got '%s'\n", line_count, data);
    exit(EXIT_FAILURE);
  }
  return TAKYON_BLOCKING;
}

static void getCollectivePathList(TakyonGraph *graph, char *data, int *count_ret, TakyonCollectiveConnection **collective_path_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  TakyonCollectiveConnection *collective_path_list = NULL;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      int path_id;
      char endpoint;
      int tokens = sscanf(keyword, "%d:%c", &path_id, &endpoint);
      if ((tokens != 2) || (path_id < 0) || (endpoint != 'A' && endpoint != 'B')) {
        fprintf(stderr, "Line %d: The format for each should should be '<path_id>:{A | B}', but got value '%s'\n", line_count, keyword);
        exit(EXIT_FAILURE);
      }
      TakyonConnection *path_desc = getPath(graph, path_id);
      if (path_desc == NULL) {
        fprintf(stderr, "Line %d: There is no path for path ID=%d\n", line_count, path_id);
        exit(EXIT_FAILURE);
      }
      count++;
      collective_path_list = realloc(collective_path_list, count * sizeof(TakyonCollectiveConnection));
      validateAllocation(collective_path_list, __FUNCTION__);
      collective_path_list[count-1].path_id = path_id;
      collective_path_list[count-1].src_is_endpointA = (endpoint == 'A');
      strcpy(data, value);
    }
  }
  *count_ret = count;
  *collective_path_list_ret = collective_path_list;
}

static void getUInt64List(char *data, int *count_ret, uint64_t **uint64_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  uint64_t *uint64_list = NULL;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      uint64_t number = getUInt64Value(keyword, line_count);
      count++;
      uint64_list = realloc(uint64_list, count * sizeof(uint64_t));
      validateAllocation(uint64_list, __FUNCTION__);
      uint64_list[count-1] = number;
      strcpy(data, value);
    }
  }
  *count_ret = count;
  *uint64_list_ret = uint64_list;
}

static int getThreadIdFromInstance(TakyonGraph *graph, char *data, int line_count) {
  // Format is <thread_group_name>[index]
  static char thread_name[MAX_VALUE_BYTES];
  size_t length = strlen(data);
  size_t open_bracket_index = 0;
  for (size_t i=0; i<length; i++) {
    if (data[i] == '[') {
      open_bracket_index = i;
      thread_name[i] = '\0';
      break;
    }
    thread_name[i] = data[i];
  }

  int thread_index;
  int tokens = sscanf(&data[open_bracket_index], "[%u]", &thread_index);
  if (tokens != 1) {
    fprintf(stderr, "Line %d: '%s' is not a valid thread identification. Must be '<thread_group_name>[index]'\n", line_count, data);
    exit(EXIT_FAILURE);
  }

  TakyonThreadGroup *thread_group_desc = takyonGetThreadGroupByName(graph, thread_name);
  if (thread_group_desc == NULL) {
    fprintf(stderr, "Line %d: The thread group with name='%s' was not defined in the 'ThreadGroups' section.\n", line_count, thread_name);
    exit(EXIT_FAILURE);
  }
  if (thread_index < 0 && thread_index >= thread_group_desc->instances) {
    fprintf(stderr, "Line %d: The thread '%s' referneces an index that is out of range: max index allowed is %d\n", line_count, data, thread_group_desc->instances-1);
    exit(EXIT_FAILURE);
  }

  int thread_id = thread_group_desc->starting_thread_id + thread_index;
  return thread_id;
}

static void getThreadList(TakyonGraph *graph, char *data, int *count_ret, int **int_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  int *int_list = NULL;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      // Check for: <thread_group_name>[index]
      static char thread_name[MAX_VALUE_BYTES];
      size_t length = strlen(keyword);
      size_t open_bracket_index = 0;
      for (size_t i=0; i<length; i++) {
        if (keyword[i] == '[') {
          open_bracket_index = i;
          thread_name[i] = '\0';
          break;
        }
        thread_name[i] = keyword[i];
      }
      int thread_index;
      int tokens = sscanf(&keyword[open_bracket_index], "[%u]", &thread_index);
      if (tokens != 1) {
        fprintf(stderr, "Line %d: '%s' is not a valid thread identification. Must be '<thread_group_name>[index]'\n", line_count, keyword);
        exit(EXIT_FAILURE);
      }
      TakyonThreadGroup *thread_group_desc = takyonGetThreadGroupByName(graph, thread_name);
      if (thread_group_desc == NULL) {
        fprintf(stderr, "Line %d: The thread group with name='%s' was not defined in the 'ThreadGroups' section.\n", line_count, thread_name);
        exit(EXIT_FAILURE);
      }
      if (thread_index < 0 && thread_index >= thread_group_desc->instances) {
        fprintf(stderr, "Line %d: The thread '%s' referneces an index that is out of range: max index allowed is %d\n", line_count, keyword, thread_group_desc->instances-1);
        exit(EXIT_FAILURE);
      }

      count++;
      int_list = realloc(int_list, count * sizeof(int));
      validateAllocation(int_list, __FUNCTION__);
      int_list[count-1] = thread_group_desc->starting_thread_id + thread_index;
      strcpy(data, value);
    }
  }
  *count_ret = count;
  *int_list_ret = int_list;
}

static void getAddrList(TakyonGraph *graph, char *data, int *count_ret, size_t **addr_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  size_t *addr_list = NULL;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      size_t number = 0;
      if (strcmp(keyword, "NULL") == 0) {
        number = 0;
      } else {
        // Check for: <mem_block_name>:<offset>
        static char mem_name[MAX_VALUE_BYTES];
        size_t length = strlen(keyword);
        size_t colon_index = 0;
        for (size_t i=0; i<length; i++) {
          if (keyword[i] == ':') {
            colon_index = i;
            mem_name[i] = '\0';
            break;
          }
          mem_name[i] = keyword[i];
        }
        unsigned long long offset;
        int tokens = sscanf(&keyword[colon_index], ":%llu", &offset);
        if (tokens != 1) {
          fprintf(stderr, "Line %d: '%s' is not a valid address. Must be 'NULL' or '<mem_block_name>:<offset>'\n", line_count, keyword);
          exit(EXIT_FAILURE);
        }
        TakyonMemoryBlock *memory_block = getMemoryBlock(graph, mem_name);
        if (memory_block == NULL) {
          fprintf(stderr, "Line %d: The memory block with name='%s' was not defined in the 'MemoryBlocks' section.\n", line_count, mem_name);
          exit(EXIT_FAILURE);
        }
        if (offset >= memory_block->bytes) {
          fprintf(stderr, "Line %d: The memory block with name='%s' does not have enough bytes for offset=%llu.\n", line_count, mem_name, offset);
          exit(EXIT_FAILURE);
        }
        number = (size_t)memory_block->addr + (size_t)offset;
      }

      count++;
      addr_list = realloc(addr_list, count * sizeof(size_t));
      validateAllocation(addr_list, __FUNCTION__);
      addr_list[count-1] = number;
      strcpy(data, value);
    }
  }
  *count_ret = count;
  *addr_list_ret = addr_list;
}

static char *parseThreadGroups(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;
  int starting_thread_id = 0;

  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKey("ThreadGroups", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("ThreadGroup:", data_ptr, keyword, value, line_count);
  while (strcmp("ThreadGroup:", keyword) == 0) {
    // Alloc list item
    graph->thread_group_count++;
    graph->thread_group_list = realloc(graph->thread_group_list, graph->thread_group_count * sizeof(TakyonThreadGroup));
    validateAllocation(graph->thread_group_list, __FUNCTION__);
    TakyonThreadGroup *thread_group_desc = &graph->thread_group_list[graph->thread_group_count - 1];
    thread_group_desc->starting_thread_id = starting_thread_id;

    // Name
    validateKeyValue("ThreadGroup:", data_ptr, keyword, value, line_count);
    thread_group_desc->name = strdup(value);
    validateAllocation(thread_group_desc->name, __FUNCTION__);
    for (int j=0; j<(graph->thread_group_count-1); j++) {
      validateUniqueNames(thread_group_desc->name, graph->thread_group_list[j].name, line_count);
    }

    // Instances
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Instances:", data_ptr, keyword, value, line_count);
    thread_group_desc->instances = getIntValue(value, line_count, 1);
    starting_thread_id += thread_group_desc->instances;

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static char *parseProcesses(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;
  int total_threads_in_process = 0;

  validateKey("Processes", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("Process:", data_ptr, keyword, value, line_count);
  while (strcmp("Process:", keyword) == 0) {
    // Alloc list item
    graph->process_count++;
    graph->process_list = realloc(graph->process_list, graph->process_count * sizeof(TakyonProcess));
    validateAllocation(graph->process_list, __FUNCTION__);
    TakyonProcess *process_desc = &graph->process_list[graph->process_count - 1];
    process_desc->memory_block_count = 0;
    process_desc->memory_block_list = NULL;

    // ID
    validateKeyValue("Process:", data_ptr, keyword, value, line_count);
    process_desc->id = getIntValue(value, line_count, 0);
    for (int j=0; j<(graph->process_count-1); j++) {
      validateUniqueInt(process_desc->id, graph->process_list[j].id, line_count);
    }

    // Threads
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Threads:", data_ptr, keyword, value, line_count);
    int *thread_id_list = NULL;
    getThreadList(graph, value, &process_desc->thread_count, &thread_id_list, line_count);
    total_threads_in_process += process_desc->thread_count;

    // Make sure no duplicates in this list
    for (int j=0; j<process_desc->thread_count; j++) {
      for (int k=j+1; k<process_desc->thread_count; k++) {
        validateUniqueInt(thread_id_list[j], thread_id_list[k], line_count);
      }
    }
    // Make sure no duplicates in other process lists
    for (int j=0; j<(graph->process_count-1); j++) {
      TakyonProcess *process_desc2 = &graph->process_list[j];
      for (int k=0; k<process_desc2->thread_count; k++) {
        for (int l=0; l<process_desc->thread_count; l++) {
          validateUniqueInt(thread_id_list[l], process_desc2->thread_list[k].id, line_count);
        }
      }
    }

    // Allocate thread desc list
    process_desc->thread_list = (TakyonThread *)calloc(process_desc->thread_count, sizeof(TakyonThread));
    validateAllocation(process_desc->thread_list, __FUNCTION__);

    // Make sure all threads in this list are unique
    for (int j=0; j<process_desc->thread_count; j++) {
      int thread_id = thread_id_list[j];
      TakyonThreadGroup *thread_group_desc = takyonGetThreadGroup(graph, thread_id);
      if (thread_group_desc == NULL) { fprintf(stderr, "Line %d: Thread ID=%d not valid\n", line_count, thread_id); exit(EXIT_FAILURE); }
      TakyonThread *thread_desc = &process_desc->thread_list[j];
      thread_desc->id = thread_id;
    }
    free(thread_id_list);

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  // Verify all threads are used in procesess
  int total_expected_threads = getTotalThreads(graph);
  if (total_threads_in_process != total_expected_threads) {
    fprintf(stderr, "Line %d: The processes only make use of %d of the %d defined threads\n", line_count, total_threads_in_process, total_expected_threads);
    exit(EXIT_FAILURE);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static char *parseMemoryBlocks(int my_process_id, TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("MemoryBlocks", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while (strcmp("MemoryBlock:", keyword) == 0) {
    // Name
    validateKeyValue("MemoryBlock:", data_ptr, keyword, value, line_count);
    char *name = strdup(value);
    validateAllocation(name, __FUNCTION__);
    // Make sure the name is unique across all processes
    for (int j=0; j<graph->process_count; j++) {
      TakyonProcess *process_desc = &graph->process_list[j];
      for (int k=0; k<process_desc->memory_block_count; k++) {
        validateUniqueNames(name, process_desc->memory_block_list[k].name, line_count);
      }
    }

    // Process ID
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("ProcessId:", data_ptr, keyword, value, line_count);
    int process_id = getIntValue(value, line_count, 0);
    TakyonProcess *process_desc = getProcess(graph, process_id);
    if (process_desc == NULL) {
      fprintf(stderr, "Line %d: Referenceing process ID=%d, but that process was not defined.\n", line_count, process_id);
      exit(EXIT_FAILURE);
    }

    // Where
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Where:", data_ptr, keyword, value, line_count);
    char *where = strdup(value);
    validateAllocation(where, __FUNCTION__);

    // Bytes
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Bytes:", data_ptr, keyword, value, line_count);
    uint64_t bytes = getUInt64Value(value, line_count);

    // Alloc list item
    process_desc->memory_block_count++;
    process_desc->memory_block_list = realloc(process_desc->memory_block_list, process_desc->memory_block_count * sizeof(TakyonMemoryBlock));
    validateAllocation(process_desc->memory_block_list, __FUNCTION__);
    TakyonMemoryBlock *memory_block = &process_desc->memory_block_list[process_desc->memory_block_count - 1];
    memory_block->name = name;
    memory_block->where = where;
    validateAllocation(memory_block->where, __FUNCTION__);
    memory_block->bytes = bytes;
    memory_block->addr = NULL;
    // NOTE: This function is application defined
    if (process_id == my_process_id) {
      // This is the process that allocates the memory
      memory_block->addr = appAllocateMemory(memory_block->name, memory_block->where, memory_block->bytes, &memory_block->user_data);
      if (memory_block->addr == NULL) {
        fprintf(stderr, "Application falied to return a valid address: takyonAllocateMemory(name='%s', where='%s', bytes=%llu)\n", memory_block->name, memory_block->where, (unsigned long long)memory_block->bytes);
        exit(EXIT_FAILURE);
      }
    }

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static TakyonCollectiveType getCollectiveTypeFromText(const char *value, int line_count) {
  if (strcmp(value, "ONE2ONE") == 0) return TAKYON_COLLECTIVE_ONE2ONE;
  if (strcmp(value, "SCATTER") == 0) return TAKYON_COLLECTIVE_SCATTER;
  if (strcmp(value, "GATHER") == 0) return TAKYON_COLLECTIVE_GATHER;
  fprintf(stderr, "Line %d: The name '%s' is not a valid collective type.\n", line_count, value);
  exit(EXIT_FAILURE);
}

static char *parseCollectiveGroups(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("CollectiveGroups", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while (strcmp("CollectiveGroup:", keyword) == 0) {
    // Alloc list item
    graph->collective_count++;
    graph->collective_list = realloc(graph->collective_list, graph->collective_count * sizeof(TakyonCollectiveGroup));
    validateAllocation(graph->collective_list, __FUNCTION__);
    TakyonCollectiveGroup *collective_desc = &graph->collective_list[graph->collective_count - 1];
    memset(collective_desc, 0, sizeof(TakyonCollectiveGroup));

    // Name
    validateKeyValue("CollectiveGroup:", data_ptr, keyword, value, line_count);
    collective_desc->name = strdup(value);
    validateAllocation(collective_desc->name, __FUNCTION__);
    for (int j=0; j<(graph->collective_count-1); j++) {
      validateUniqueNames(collective_desc->name, graph->collective_list[j].name, line_count);
    }

    // Type
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Type:", data_ptr, keyword, value, line_count);
    collective_desc->type = getCollectiveTypeFromText(value, line_count);

    // Get path list: <path_id>:{A | B}
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("PathSrcIds:", data_ptr, keyword, value, line_count);
    getCollectivePathList(graph, value, &collective_desc->num_paths, &collective_desc->path_list, line_count);

    // Verify all paths are unique
    for (int i=0; i<collective_desc->num_paths; i++) {
      for (int j=i+1; j<collective_desc->num_paths; j++) {
        TakyonCollectiveConnection *coll_path1 = &collective_desc->path_list[i];
        TakyonCollectiveConnection *coll_path2 = &collective_desc->path_list[j];
        if (coll_path1->path_id == coll_path2->path_id) {
          fprintf(stderr, "Path ID=%d is defined multiple times in the collective group '%s'\n", coll_path1->path_id, collective_desc->name);
          exit(EXIT_FAILURE);
        }
      }
    }

    // Validate collective connectivity
    if (collective_desc->type == TAKYON_COLLECTIVE_ONE2ONE) {
      // Nothing extra to validate
    } else if (collective_desc->type == TAKYON_COLLECTIVE_SCATTER) {
      // Make sure all scatter sources are the same
      TakyonCollectiveConnection *first_coll_path = &collective_desc->path_list[0];
      TakyonConnection *first_path_desc = getPath(graph, first_coll_path->path_id);
      int src_thread_id = first_coll_path->src_is_endpointA ? first_path_desc->thread_idA : first_path_desc->thread_idB;
      for (int i=1; i<collective_desc->num_paths; i++) {
        TakyonCollectiveConnection *coll_path = &collective_desc->path_list[i];
        TakyonConnection *path_desc = getPath(graph, coll_path->path_id);
        int thread_id = coll_path->src_is_endpointA ? path_desc->thread_idA : path_desc->thread_idB;
        if (thread_id != src_thread_id) {
          fprintf(stderr, "Collective group '%s' is a scatter, but the source thread IDs are not all the same.\n", collective_desc->name);
          exit(EXIT_FAILURE);
        }
      }
    } else if (collective_desc->type == TAKYON_COLLECTIVE_GATHER) {
      // Make sure all gather destinations are the same
      TakyonCollectiveConnection *first_coll_path = &collective_desc->path_list[0];
      TakyonConnection *first_path_desc = getPath(graph, first_coll_path->path_id);
      int dest_thread_id = first_coll_path->src_is_endpointA ? first_path_desc->thread_idB : first_path_desc->thread_idA;
      for (int i=1; i<collective_desc->num_paths; i++) {
        TakyonCollectiveConnection *coll_path = &collective_desc->path_list[i];
        TakyonConnection *path_desc = getPath(graph, coll_path->path_id);
        int thread_id = coll_path->src_is_endpointA ? path_desc->thread_idB : path_desc->thread_idA;
        if (thread_id != dest_thread_id) {
          fprintf(stderr, "Collective group '%s' is a gather, but the destination thread IDs are not all the same.\n", collective_desc->name);
          exit(EXIT_FAILURE);
        }
      }
    }

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

#define MAX_PARAM_CHARS 30
#define NUM_DEFAULT_PARAMS 16
#define NUM_PATH_PARAMS (NUM_DEFAULT_PARAMS+1)
typedef enum {
  PARAM_TYPE_BOOL,
  PARAM_TYPE_INT,
  PARAM_TYPE_THREAD_INSTANCE,
  PARAM_TYPE_UINT64,
  PARAM_TYPE_DOUBLE,
  PARAM_TYPE_COMPLETION_METHOD,
  PARAM_TYPE_UINT64_LIST,
  PARAM_TYPE_ADDR_LIST,
} ParamType;

typedef struct {
  char name[MAX_PARAM_CHARS];
  ParamType type;
  bool boolA, boolB;
  int intA, intB;
  uint64_t uint64A, uint64B;
  double doubleA, doubleB;
  TakyonCompletionMethod completetionA, completetionB;
  int listA_count, listB_count;
  uint64_t *uint64_listA, *uint64_listB;
  size_t *addr_listA, *addr_listB;
} PathParam;

static void setParamDefaultValueBool(PathParam *param, const char *name, bool value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_BOOL;
  param->boolA = value;
  param->boolB = value;
}

static void setParamDefaultValueThreadInstance(PathParam *param, const char *name, int value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_THREAD_INSTANCE;
  param->intA = value;
  param->intB = value;
}

static void setParamDefaultValueInt(PathParam *param, const char *name, int value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_INT;
  param->intA = value;
  param->intB = value;
}

static void setParamDefaultValueUInt64(PathParam *param, const char *name, uint64_t value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_UINT64;
  param->uint64A = value;
  param->uint64B = value;
}

static void setParamDefaultValueDouble(PathParam *param, const char *name, double value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_DOUBLE;
  param->doubleA = value;
  param->doubleB = value;
}

static void setParamDefaultValueCompletionMethod(PathParam *param, const char *name, TakyonCompletionMethod value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_COMPLETION_METHOD;
  param->completetionA = value;
  param->completetionB = value;
}

static void setParamDefaultValueUInt64List(PathParam *param, const char *name, uint64_t value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_UINT64_LIST;
  param->uint64_listA = (uint64_t *)malloc(sizeof(uint64_t));
  param->uint64_listB = (uint64_t *)malloc(sizeof(uint64_t));
  if ((param->uint64_listA == NULL) || (param->uint64_listB == NULL)) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  param->uint64_listA[0] = value;
  param->uint64_listB[0] = value;
  param->listA_count = 1;
  param->listB_count = 1;
}

static void setParamDefaultValueAddrList(PathParam *param, const char *name, size_t value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_ADDR_LIST;
  param->addr_listA = (size_t *)malloc(sizeof(size_t));
  param->addr_listB = (size_t *)malloc(sizeof(size_t));
  if ((param->addr_listA == NULL) || (param->addr_listB == NULL)) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  param->addr_listA[0] = value;
  param->addr_listB[0] = value;
}

static void updateParam(TakyonGraph *graph, PathParam *param, char *value, int line_count) {
  static char valueA[MAX_VALUE_BYTES];
  static char valueB[MAX_VALUE_BYTES];

  // Verify one one comma
  int comma_count = 0;
  for (int i=0; i<strlen(value); i++) {
    if (value[i] == ',') comma_count++;
  }
  if (comma_count != 1) {
    fprintf(stderr, "Line %d: There should a single comma separating endpoint A and B values.\n", line_count);
    exit(EXIT_FAILURE);
  }

  // Get A value
  int start_index = 0;
  for (int i=0; i<strlen(value); i++) {
    if (value[i] == ',') {
      start_index = i+1;
      valueA[i] = '\0';
      break;
    }
    valueA[i] = value[i];
  }    
  // Remove ending white space
  size_t length = strlen(valueA)-1;
  while ((length>0) && is_space(valueA[length])) {
    valueA[length] = '\0';
    length--;
  }

  // Get B value
  while ((value[start_index] != '\0') && is_space(value[start_index])) {
    start_index++;
  }
  for (int i=start_index; i<=strlen(value); i++) {
    valueB[i-start_index] = value[i];
  }
  // Remove ending white space
  length = strlen(valueB)-1;
  while ((length>0) && is_space(valueB[length])) {
    valueB[length] = '\0';
    length--;
  }

  // Record the values
  if (param->type == PARAM_TYPE_BOOL) {
    param->boolA = getBoolValue(valueA, line_count);
    param->boolB = getBoolValue(valueB, line_count);
  } else if (param->type == PARAM_TYPE_THREAD_INSTANCE) {
    // Format is <thread_group_name>[index]
    param->intA = getThreadIdFromInstance(graph, valueA, line_count);
    param->intB = getThreadIdFromInstance(graph, valueB, line_count);
  } else if (param->type == PARAM_TYPE_INT) {
    param->intA = getIntValue(valueA, line_count, 0);
    param->intB = getIntValue(valueB, line_count, 0);
  } else if (param->type == PARAM_TYPE_UINT64) {
    param->uint64A = getVerbosityValue(valueA, line_count);
    param->uint64B = getVerbosityValue(valueB, line_count);
  } else if (param->type == PARAM_TYPE_DOUBLE) {
    param->doubleA = getDoubleValue(valueA, line_count);
    param->doubleB = getDoubleValue(valueB, line_count);
  } else if (param->type == PARAM_TYPE_COMPLETION_METHOD) {
    param->completetionA = getCompletionMethod(valueA, line_count);
    param->completetionB = getCompletionMethod(valueB, line_count);
  } else if (param->type == PARAM_TYPE_UINT64_LIST) {
    free(param->uint64_listA);
    free(param->uint64_listB);
    getUInt64List(valueA, &param->listA_count, &param->uint64_listA, line_count);
    getUInt64List(valueB, &param->listB_count, &param->uint64_listB, line_count);
  } else if (param->type == PARAM_TYPE_ADDR_LIST) {
    free(param->addr_listA);
    free(param->addr_listB);
    getAddrList(graph, valueA, &param->listA_count, &param->addr_listA, line_count);
    getAddrList(graph, valueB, &param->listB_count, &param->addr_listB, line_count);
  }
}

static int getParamIndex(const char *name, PathParam *params, int num_params) {
  for (int i=0; i<num_params; i++) {
    PathParam *param = &params[i];
    if (strcmp(param->name, name) == 0) return i;
  }
  return -1;
}

static void copyParams(PathParam *dest_params, PathParam *src_params, int num_params) {
  for (int i=0; i<num_params; i++) {
    strncpy(dest_params[i].name, src_params[i].name, MAX_PARAM_CHARS);
    dest_params[i].type = src_params[i].type;
    if (dest_params[i].type == PARAM_TYPE_BOOL) {
      dest_params[i].boolA = src_params[i].boolA;
      dest_params[i].boolB = src_params[i].boolB;
    } else if (dest_params[i].type == PARAM_TYPE_THREAD_INSTANCE) {
      dest_params[i].intA = src_params[i].intA;
      dest_params[i].intB = src_params[i].intB;
    } else if (dest_params[i].type == PARAM_TYPE_INT) {
      dest_params[i].intA = src_params[i].intA;
      dest_params[i].intB = src_params[i].intB;
    } else if (dest_params[i].type == PARAM_TYPE_UINT64) {
      dest_params[i].uint64A = src_params[i].uint64A;
      dest_params[i].uint64B = src_params[i].uint64B;
    } else if (dest_params[i].type == PARAM_TYPE_DOUBLE) {
      dest_params[i].doubleA = src_params[i].doubleA;
      dest_params[i].doubleB = src_params[i].doubleB;
    } else if (dest_params[i].type == PARAM_TYPE_COMPLETION_METHOD) {
      dest_params[i].completetionA = src_params[i].completetionA;
      dest_params[i].completetionB = src_params[i].completetionB;
    } else if (dest_params[i].type == PARAM_TYPE_UINT64_LIST) {
      dest_params[i].listA_count = src_params[i].listA_count;
      dest_params[i].listB_count = src_params[i].listB_count;
      dest_params[i].uint64_listA = (uint64_t *)malloc(dest_params[i].listA_count * sizeof(uint64_t));
      dest_params[i].uint64_listB = (uint64_t *)malloc(dest_params[i].listB_count * sizeof(uint64_t));
      if ((dest_params[i].uint64_listA == NULL) || (dest_params[i].uint64_listB == NULL)) {
        fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      for (int j=0; j<dest_params[i].listA_count; j++) dest_params[i].uint64_listA[j] = src_params[i].uint64_listA[j];
      for (int j=0; j<dest_params[i].listB_count; j++) dest_params[i].uint64_listB[j] = src_params[i].uint64_listB[j];
    } else if (dest_params[i].type == PARAM_TYPE_ADDR_LIST) {
      dest_params[i].listA_count = src_params[i].listA_count;
      dest_params[i].listB_count = src_params[i].listB_count;
      dest_params[i].addr_listA = (size_t *)malloc(dest_params[i].listA_count * sizeof(size_t));
      dest_params[i].addr_listB = (size_t *)malloc(dest_params[i].listB_count * sizeof(size_t));
      if ((dest_params[i].addr_listA == NULL) || (dest_params[i].addr_listB == NULL)) {
        fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      for (int j=0; j<dest_params[i].listA_count; j++) dest_params[i].addr_listA[j] = src_params[i].addr_listA[j];
      for (int j=0; j<dest_params[i].listB_count; j++) dest_params[i].addr_listB[j] = src_params[i].addr_listB[j];
    }
  }
}

static uint64_t *copyUInt64List(uint64_t *list, int count) {
  uint64_t *new_list = (uint64_t *)malloc(count * sizeof(uint64_t));
  if (new_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<count; i++) {
    new_list[i] = list[i];
  }
  return new_list;
}

static size_t *copyAddrList(size_t *list, int count) {
  size_t *new_list = (size_t *)malloc(count * sizeof(size_t));
  if (new_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<count; i++) {
    new_list[i] = list[i];
  }
  return new_list;
}

static void freeParamLists(PathParam *path_params, int num_params) {
  for (int i=0; i<num_params; i++) {
    PathParam *param = &path_params[i];
    if ((strcmp(param->name, "SenderMaxBytesList:") == 0) || (strcmp(param->name, "RecverMaxBytesList:") == 0)) {
      free(param->uint64_listA);
      free(param->uint64_listB);
    } else if ((strcmp(param->name, "SenderAddrList:") == 0) || (strcmp(param->name, "RecverAddrList:") == 0)) {
      free(param->addr_listA);
      free(param->addr_listB);
    }
  }
}

static void copyParamsToPathAttributes(TakyonPathAttributes *attrs, bool is_endpointA, PathParam *path_params, int num_params, const char *interconnectA, const char *interconnectB) {
  attrs->is_endpointA = is_endpointA;
  strncpy(attrs->interconnect, is_endpointA ? interconnectA : interconnectB, MAX_TAKYON_INTERCONNECT_CHARS);
  for (int i=0; i<num_params; i++) {
    PathParam *param = &path_params[i];
    if (strcmp(param->name, "IsPolling:") == 0) {
      attrs->is_polling = is_endpointA ? param->boolA : param->boolB;
    } else if (strcmp(param->name, "AbortOnFailure:") == 0) {
      attrs->abort_on_failure = is_endpointA ? param->boolA : param->boolB;
    } else if (strcmp(param->name, "Verbosity:") == 0) {
      attrs->verbosity = is_endpointA ? param->uint64A : param->uint64B;
    } else if (strcmp(param->name, "CreateTimeout:") == 0) {
      attrs->create_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "SendStartTimeout:") == 0) {
      attrs->send_start_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "SendCompleteTimeout:") == 0) {
      attrs->send_complete_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "RecvCompleteTimeout:") == 0) {
      attrs->recv_complete_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "DestroyTimeout:") == 0) {
      attrs->destroy_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "SendCompletionMethod:") == 0) {
      attrs->send_completion_method = is_endpointA ? param->completetionA : param->completetionB;
    } else if (strcmp(param->name, "RecvCompletionMethod:") == 0) {
      attrs->recv_completion_method = is_endpointA ? param->completetionA : param->completetionB;
    } else if (strcmp(param->name, "NBufsAtoB:") == 0) {
      attrs->nbufs_AtoB = is_endpointA ? param->intA : param->intB;
    } else if (strcmp(param->name, "NBufsBtoA:") == 0) {
      attrs->nbufs_BtoA = is_endpointA ? param->intA : param->intB;
    } else if (strcmp(param->name, "SenderMaxBytesList:") == 0) {
      attrs->sender_max_bytes_list = copyUInt64List(is_endpointA ? param->uint64_listA : param->uint64_listB, is_endpointA ? param->listA_count : param->listB_count);
    } else if (strcmp(param->name, "RecverMaxBytesList:") == 0) {
      attrs->recver_max_bytes_list = copyUInt64List(is_endpointA ? param->uint64_listA : param->uint64_listB, is_endpointA ? param->listA_count : param->listB_count);
    } else if (strcmp(param->name, "SenderAddrList:") == 0) {
      attrs->sender_addr_list = copyAddrList(is_endpointA ? param->addr_listA : param->addr_listB, is_endpointA ? param->listA_count : param->listB_count);
    } else if (strcmp(param->name, "RecverAddrList:") == 0) {
      attrs->recver_addr_list = copyAddrList(is_endpointA ? param->addr_listA : param->addr_listB, is_endpointA ? param->listA_count : param->listB_count);
    } else {
      fprintf(stderr, "%s(): Unknown param name: '%s'\n", __FUNCTION__, param->name);
      exit(EXIT_FAILURE);
    }
  }
}

static char *parsePaths(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  PathParam default_params[NUM_DEFAULT_PARAMS];
  memset(default_params, 0, NUM_DEFAULT_PARAMS*sizeof(PathParam));
  setParamDefaultValueBool(&default_params[0], "IsPolling:", false);
  setParamDefaultValueBool(&default_params[1], "AbortOnFailure:", true);
  setParamDefaultValueUInt64(&default_params[2], "Verbosity:", TAKYON_VERBOSITY_ERRORS);
  setParamDefaultValueDouble(&default_params[3], "CreateTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[4], "SendStartTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[5], "SendCompleteTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[6], "RecvCompleteTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[7], "DestroyTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueCompletionMethod(&default_params[8], "SendCompletionMethod:", TAKYON_BLOCKING);
  setParamDefaultValueCompletionMethod(&default_params[9], "RecvCompletionMethod:", TAKYON_BLOCKING);
  setParamDefaultValueInt(&default_params[10], "NBufsAtoB:", 1);
  setParamDefaultValueInt(&default_params[11], "NBufsBtoA:", 1);
  setParamDefaultValueUInt64List(&default_params[12], "SenderMaxBytesList:", 0);
  setParamDefaultValueUInt64List(&default_params[13], "RecverMaxBytesList:", 0);
  setParamDefaultValueAddrList(&default_params[14], "SenderAddrList:", 0);
  setParamDefaultValueAddrList(&default_params[15], "RecverAddrList:", 0);

  /*+ need to handle unconnected paths */
  validateKey("Paths", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while ((strcmp("Defaults", keyword) == 0) || (strcmp("Path:", keyword) == 0)) {
    if (strcmp("Defaults", keyword) == 0) {
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      int param_index = getParamIndex(keyword, default_params, NUM_DEFAULT_PARAMS);
      while (param_index >= 0) {
        // Update default list
        updateParam(graph, &default_params[param_index], value, line_count);
        // Get next line
        data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
        param_index = getParamIndex(keyword, default_params, NUM_DEFAULT_PARAMS);
      }
    } else {
      // Alloc list item
      graph->path_count++;
      graph->path_list = realloc(graph->path_list, graph->path_count * sizeof(TakyonConnection));
      validateAllocation(graph->path_list, __FUNCTION__);
      TakyonConnection *path_desc = &graph->path_list[graph->path_count - 1];
      path_desc->pathA = NULL;
      path_desc->pathB = NULL;

      // ID
      validateKeyValue("Path:", data_ptr, keyword, value, line_count);
      path_desc->id = getIntValue(value, line_count, 0);
      for (int j=0; j<(graph->path_count-1); j++) {
        validateUniqueInt(path_desc->id, graph->path_list[j].id, line_count);
      }

      /*+ require Thread: and InterconnectA:, and InterconnectB: before other things */
      // Path attributes
      PathParam path_params[NUM_PATH_PARAMS];
      memset(path_params, 0, NUM_PATH_PARAMS*sizeof(PathParam));
      static char interconnectA[MAX_TAKYON_INTERCONNECT_CHARS];
      static char interconnectB[MAX_TAKYON_INTERCONNECT_CHARS];
      copyParams(path_params, default_params, NUM_DEFAULT_PARAMS);
      setParamDefaultValueThreadInstance(&path_params[NUM_DEFAULT_PARAMS], "Thread:", -1);
      interconnectA[0] = '\0';
      interconnectB[0] = '\0';

      // Get path attributes
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      int param_index = getParamIndex(keyword, path_params, NUM_PATH_PARAMS);
      while ((param_index >= 0) || (strcmp(keyword,"InterconnectA:")==0) || (strcmp(keyword,"InterconnectB:")==0)) {
        if (param_index >= 0) {
          // Update path params
          updateParam(graph, &path_params[param_index], value, line_count);
        } else if (strcmp(keyword,"InterconnectA:")==0) {
          strncpy(interconnectA, value, MAX_TAKYON_INTERCONNECT_CHARS);
        } else if (strcmp(keyword,"InterconnectB:")==0) {
          strncpy(interconnectB, value, MAX_TAKYON_INTERCONNECT_CHARS);
        }
        // Get next line
        data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
        param_index = getParamIndex(keyword, path_params, NUM_PATH_PARAMS);
      }

      // Do some error checking before setting the Takyon path attributes
      if (strlen(interconnectA) == 0) {
        fprintf(stderr, "Line %d: 'InterconnectA' was not defined for this path\n", line_count-1);
        exit(EXIT_FAILURE);
      }
      if (strlen(interconnectB) == 0) {
        fprintf(stderr, "Line %d: 'InterconnectB' was not defined for this path\n", line_count-1);
        exit(EXIT_FAILURE);
      }
      if ((path_params[NUM_DEFAULT_PARAMS].intA < 0) || (path_params[NUM_DEFAULT_PARAMS].intB < 0)) {
        fprintf(stderr, "Line %d: 'ThreadId' for A and B was not defined for this path\n", line_count-1);
        exit(EXIT_FAILURE);
      }
      if (path_params[NUM_DEFAULT_PARAMS].intA == path_params[NUM_DEFAULT_PARAMS].intB) {
        fprintf(stderr, "Line %d: 'ThreadId' for A and B can not be the same\n", line_count-1);
        exit(EXIT_FAILURE);
      }

      // Validate endpoint A buffer counts
      param_index = getParamIndex("NBufsAtoB:", path_params, NUM_PATH_PARAMS);
      int nbufs_AtoB = path_params[param_index].intA;
      param_index = getParamIndex("NBufsBtoA:", path_params, NUM_PATH_PARAMS);
      int nbufs_BtoA = path_params[param_index].intA;
      param_index = getParamIndex("SenderMaxBytesList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_AtoB != path_params[param_index].listA_count) {
        fprintf(stderr, "Line %d: 'NBufsAtoB'=%d for endpointA does not match the number of values for 'SenderMaxBytesList'\n", line_count-1, nbufs_AtoB);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("RecverMaxBytesList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_BtoA != path_params[param_index].listA_count) {
        fprintf(stderr, "Line %d: 'NBufsBtoA'=%d for endpointA does not match the number of values for 'RecverMaxBytesList'\n", line_count-1, nbufs_BtoA);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("SenderAddrList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_AtoB != path_params[param_index].listA_count) {
        fprintf(stderr, "Line %d: 'NBufsAtoB'=%d for endpointA does not match the number of values for 'SenderAddrList'\n", line_count-1, nbufs_AtoB);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("RecverAddrList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_BtoA != path_params[param_index].listA_count) {
        fprintf(stderr, "Line %d: 'NBufsBtoA'=%d for endpointA does not match the number of values for 'RecverAddrList'\n", line_count-1, nbufs_BtoA);
        exit(EXIT_FAILURE);
      }

      // Validate endpoint B buffer counts
      param_index = getParamIndex("NBufsAtoB:", path_params, NUM_PATH_PARAMS);
      nbufs_AtoB = path_params[param_index].intB;
      param_index = getParamIndex("NBufsBtoA:", path_params, NUM_PATH_PARAMS);
      nbufs_BtoA = path_params[param_index].intB;
      param_index = getParamIndex("SenderMaxBytesList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_BtoA != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsAtoB'=%d for endpointB does not match the number of values for 'SenderMaxBytesList'\n", line_count-1, nbufs_BtoA);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("RecverMaxBytesList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_AtoB != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsBtoA'=%d for endpointB does not match the number of values for 'RecverMaxBytesList'\n", line_count-1, nbufs_AtoB);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("SenderAddrList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_BtoA != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsAtoB'=%d for endpointB does not match the number of values for 'SenderAddrList'\n", line_count-1, nbufs_BtoA);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("RecverAddrList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_AtoB != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsBtoA'=%d for endpointB does not match the number of values for 'RecverAddrList'\n", line_count-1, nbufs_AtoB);
        exit(EXIT_FAILURE);
      }

      // Set Takyon path attributes
      path_desc->thread_idA = path_params[NUM_DEFAULT_PARAMS].intA;
      path_desc->thread_idB = path_params[NUM_DEFAULT_PARAMS].intB;
      copyParamsToPathAttributes(&path_desc->attrsA, true, path_params, NUM_DEFAULT_PARAMS, interconnectA, interconnectB);
      copyParamsToPathAttributes(&path_desc->attrsB, false, path_params, NUM_DEFAULT_PARAMS, interconnectA, interconnectB);

      freeParamLists(path_params, NUM_PATH_PARAMS);
    }
  }
  freeParamLists(default_params, NUM_DEFAULT_PARAMS);

  *line_count_ret = line_count;
  return data_ptr;
}

TakyonGraph *takyonLoadGraphDescription(int process_id, const char *filename) {
  char *file_data = loadFile(filename);
  char *data_ptr = file_data;
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int line_count = 1;

  TakyonGraph *graph = (TakyonGraph *)calloc(1, sizeof(TakyonGraph));
  validateAllocation(graph, __FUNCTION__);

  // Thread Groups
  data_ptr = parseThreadGroups(graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("ThreadGroups = %d\n", graph->thread_group_count);
  for (int i=0; i<graph->thread_group_count; i++) {
    TakyonThreadGroup *thread_group_desc = &graph->thread_group_list[i];
    printf("  %s:", thread_group_desc->name);
    for (int j=0; j<thread_group_desc->thread_count; j++) {
      printf(" %d", thread_group_desc->thread_id_list[j]);
    }
    printf("\n");
  }
#endif

  // Processes
  data_ptr = parseProcesses(graph, data_ptr, keyword, value, &line_count);
  // MemoryBlocks (get mapped to a process)
  data_ptr = parseMemoryBlocks(process_id, graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Processes = %d\n", graph->process_count);
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    printf("  %d:", process_desc->id);
    for (int j=0; j<process_desc->thread_count; j++) {
      printf(" %d", process_desc->thread_list[j].id);
    }
    printf("\n");
    for (int j=0; j<process_desc->memory_block_count; j++) {
      TakyonMemoryBlock *memory_block = &process_desc->memory_block_list[j];
      printf("    MemBlock %d: where='%s', bytes=%lld, addr=0x%llx\n", memory_block->id, memory_block->where, (unsigned long long)memory_block->bytes, (unsigned long long)memory_block->addr);
    }
  }
#endif

  // Paths
  data_ptr = parsePaths(graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Paths = %d\n", graph->path_count);
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    printf("  Path=%d: ThreadA=%d, ThreadB=%d\n", path_desc->id, path_desc->thread_idA, path_desc->thread_idB);

    // Endpoint A
    printf("    Endpoint A Attributes:\n");
    printf("      Interconnect: %s\n", path_desc->attrsA.interconnect);
    printf("      is_polling: %s\n", path_desc->attrsA.is_polling ? "Yes" : "No");
    printf("      abort_on_failure: %s\n", path_desc->attrsA.abort_on_failure ? "Yes" : "No");
    printf("      verbosity: 0x%llx\n", (unsigned long long)path_desc->attrsA.verbosity);
    printf("      create_timeout: %lf\n", path_desc->attrsA.create_timeout);
    printf("      send_start_timeout: %lf\n", path_desc->attrsA.send_start_timeout);
    printf("      send_complete_timeout: %lf\n", path_desc->attrsA.send_complete_timeout);
    printf("      recv_complete_timeout: %lf\n", path_desc->attrsA.recv_complete_timeout);
    printf("      destroy_timeout: %lf\n", path_desc->attrsA.destroy_timeout);
    printf("      send_completion_method: %s\n", (path_desc->attrsA.send_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_SEND_TEST");
    printf("      recv_completion_method: %s\n", (path_desc->attrsA.recv_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_SEND_TEST");
    printf("      nbufs_AtoB: %d\n", path_desc->attrsA.nbufs_AtoB);
    printf("      nbufs_BtoA: %d\n", path_desc->attrsA.nbufs_BtoA);
    printf("      sender_max_bytes_list:");
    for (int j=0; j<path_desc->attrsA.nbufs_AtoB; j++) {
      printf(" %llu", (unsigned long long)path_desc->attrsA.sender_max_bytes_list[j]);
    }
    printf("\n");
    printf("      recver_max_bytes_list:");
    for (int j=0; j<path_desc->attrsA.nbufs_BtoA; j++) {
      printf(" %llu", (unsigned long long)path_desc->attrsA.recver_max_bytes_list[j]);
    }
    printf("\n");
    printf("      sender_addr_list:");
    for (int j=0; j<path_desc->attrsA.nbufs_AtoB; j++) {
      printf(" 0x%llx", (unsigned long long)path_desc->attrsA.sender_addr_list[j]);
    }
    printf("\n");
    printf("      recver_addr_list:");
    for (int j=0; j<path_desc->attrsA.nbufs_BtoA; j++) {
      printf(" 0x%llx", (unsigned long long)path_desc->attrsA.recver_addr_list[j]);
    }
    printf("\n");

    // Endpoint B
    printf("    Endpoint B Attributes:\n");
    printf("      Interconnect: %s\n", path_desc->attrsB.interconnect);
    printf("      is_polling: %s\n", path_desc->attrsB.is_polling ? "Yes" : "No");
    printf("      abort_on_failure: %s\n", path_desc->attrsB.abort_on_failure ? "Yes" : "No");
    printf("      verbosity: 0x%llx\n", (unsigned long long)path_desc->attrsB.verbosity);
    printf("      create_timeout: %lf\n", path_desc->attrsB.create_timeout);
    printf("      send_start_timeout: %lf\n", path_desc->attrsB.send_start_timeout);
    printf("      send_complete_timeout: %lf\n", path_desc->attrsB.send_complete_timeout);
    printf("      recv_complete_timeout: %lf\n", path_desc->attrsB.recv_complete_timeout);
    printf("      destroy_timeout: %lf\n", path_desc->attrsB.destroy_timeout);
    printf("      send_completion_method: %s\n", (path_desc->attrsB.send_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_SEND_TEST");
    printf("      recv_completion_method: %s\n", (path_desc->attrsB.recv_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_SEND_TEST");
    printf("      nbufs_AtoB: %d\n", path_desc->attrsB.nbufs_AtoB);
    printf("      nbufs_BtoA: %d\n", path_desc->attrsB.nbufs_BtoA);
    printf("      sender_max_bytes_list:");
    for (int j=0; j<path_desc->attrsB.nbufs_AtoB; j++) {
      printf(" %llu", (unsigned long long)path_desc->attrsB.sender_max_bytes_list[j]);
    }
    printf("\n");
    printf("      recver_max_bytes_list:");
    for (int j=0; j<path_desc->attrsB.nbufs_BtoA; j++) {
      printf(" %llu", (unsigned long long)path_desc->attrsB.recver_max_bytes_list[j]);
    }
    printf("\n");
    printf("      sender_addr_list:");
    for (int j=0; j<path_desc->attrsB.nbufs_AtoB; j++) {
      printf(" %llu", (unsigned long long)path_desc->attrsB.sender_addr_list[j]);
    }
    printf("\n");
    printf("      recver_addr_list:");
    for (int j=0; j<path_desc->attrsB.nbufs_BtoA; j++) {
      printf(" %llu", (unsigned long long)path_desc->attrsB.recver_addr_list[j]);
    }
    printf("\n");

  }
#endif

  // Collective groups
  data_ptr = parseCollectiveGroups(graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Collectives = %d\n", graph->collective_count);
  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollectiveGroup *collective_desc = &graph->collective_list[i];
    printf("  '%s', Type=%s, Paths:", collective_desc->name, collectiveTypeToName(collective_desc->type));
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *path = &collective_desc->path_list[j];
      printf(" %d:%s", path->path_id, path->src_is_endpointA ? "A->B" : "B->A");
    }
    printf("\n");
  }
#endif

  // Verify nothing else to parse
  if (data_ptr != NULL) {
    fprintf(stderr, "Line %d: Was expecting end of file, but got keyword '%s'\n", line_count, keyword);
    exit(EXIT_FAILURE);
  }

  // Clean up
  free(file_data);

  return graph;
}

void takyonFreeGraphDescription(TakyonGraph *graph, int my_process_id) {
  for (int i=0; i<graph->thread_group_count; i++) {
    free(graph->thread_group_list[i].name);
  }
  free(graph->thread_group_list);

  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process = &graph->process_list[i];
    free(process->thread_list);
    for (int j=0; j<process->memory_block_count; j++) {
      TakyonMemoryBlock *block = &process->memory_block_list[j];
      // NOTE: This function is application defined
      if (process->id == my_process_id) {
        // This is the process that allocated the memory
        appFreeMemory(block->where, block->user_data, block->addr);
      }
      free(block->name);
      free(block->where);
    }
    if (process->memory_block_count > 0) free(process->memory_block_list);
  }
  free(graph->process_list);

  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    takyonFreeAttributes(path_desc->attrsA);
    takyonFreeAttributes(path_desc->attrsB);
  }
  free(graph->path_list);

  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollectiveGroup *collective = &graph->collective_list[i];
    free(collective->name);
    free(collective->path_list);
  }
  if (graph->collective_count > 0) free(graph->collective_list);

  free(graph);
}

void takyonCreateGraphPaths(TakyonGraph *graph, int thread_id) {
  // Create all the paths
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    if (path_desc->thread_idA == thread_id) {
      TakyonPathAttributes *attrs = &path_desc->attrsA;
      path_desc->pathA = takyonCreate(attrs);
      if (path_desc->pathA == NULL) {
        fprintf(stderr, "%s(): Failed to create path: path=%s\n", __FUNCTION__, attrs->interconnect);
        exit(EXIT_FAILURE);
      }
    } else if (path_desc->thread_idB == thread_id) {
      TakyonPathAttributes *attrs = &path_desc->attrsB;
      path_desc->pathB = takyonCreate(attrs);
      if (path_desc->pathB == NULL) {
        fprintf(stderr, "%s(): Failed to create path: path=%s\n", __FUNCTION__, attrs->interconnect);
        exit(EXIT_FAILURE);
      }
    }
  }
}

void takyonDestroyGraphPaths(TakyonGraph *graph, int thread_id) {
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    if (path_desc->thread_idA == thread_id) {
      char *error_message = takyonDestroy(&path_desc->pathA);
      if (error_message != NULL) {
        fprintf(stderr, "%s(): Failed to destroy endpoint A of path: %s\n", __FUNCTION__, error_message);
        free(error_message);
        exit(EXIT_FAILURE);
      }
    } else if (path_desc->thread_idB == thread_id) {
      char *error_message = takyonDestroy(&path_desc->pathB);
      if (error_message != NULL) {
        fprintf(stderr, "%s(): Failed to destroy endpoint B of path: %s\n", __FUNCTION__, error_message);
        free(error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

TakyonCollectiveOne2One *takyonGetOne2One(TakyonGraph *graph, const char *name, int thread_id) {
  TakyonCollectiveOne2One *collective = NULL;
  TakyonCollectiveGroup *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_ONE2ONE)) {
    TakyonPath **src_path_list = (TakyonPath **)calloc(collective_desc->num_paths, sizeof(TakyonPath *));
    validateAllocation(src_path_list, __FUNCTION__);
    TakyonPath **dest_path_list = (TakyonPath **)calloc(collective_desc->num_paths, sizeof(TakyonPath *));
    validateAllocation(dest_path_list, __FUNCTION__);
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->thread_idA == thread_id)) || ((!collective_path->src_is_endpointA) && (path_desc->thread_idB == thread_id))) {
        src_path_list[j] = (collective_path->src_is_endpointA) ? path_desc->pathA: path_desc->pathB;
      }
      if ((collective_path->src_is_endpointA && (path_desc->thread_idB == thread_id)) || ((!collective_path->src_is_endpointA) && (path_desc->thread_idA == thread_id))) {
        dest_path_list[j] = (collective_path->src_is_endpointA) ? path_desc->pathB: path_desc->pathA;
      }
    }
    collective = takyonOne2OneInit(collective_desc->num_paths, src_path_list, dest_path_list);
    free(src_path_list);
    free(dest_path_list);
  }
  return collective;
}

TakyonScatterSrc *takyonGetScatterSrc(TakyonGraph *graph, const char *name, int thread_id) {
  TakyonScatterSrc *scatter_src = NULL;
  TakyonCollectiveGroup *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_SCATTER)) {
    TakyonCollectiveConnection *collective_path = &collective_desc->path_list[0];
    TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
    if ((collective_path->src_is_endpointA && (path_desc->thread_idA == thread_id)) || ((!collective_path->src_is_endpointA) && (path_desc->thread_idB == thread_id))) {
      // This is the scatter source
      TakyonPath **path_list = (TakyonPath **)malloc(collective_desc->num_paths * sizeof(TakyonPath *));
      validateAllocation(path_list, __FUNCTION__);
      for (int j=0; j<collective_desc->num_paths; j++) {
        TakyonCollectiveConnection *collective_path2 = &collective_desc->path_list[j];
        TakyonConnection *path_desc2 = getPath(graph, collective_path2->path_id);
        path_list[j] = (collective_path->src_is_endpointA) ? path_desc2->pathA: path_desc2->pathB;
      }
      scatter_src = takyonScatterSrcInit(collective_desc->num_paths, path_list);
      free(path_list);
    }
  }
  return scatter_src;
}

TakyonScatterDest *takyonGetScatterDest(TakyonGraph *graph, const char *name, int thread_id) {
  TakyonScatterDest *scatter_dest = NULL;
  TakyonCollectiveGroup *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_SCATTER)) {
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path2 = &collective_desc->path_list[j];
      TakyonConnection *path_desc2 = getPath(graph, collective_path2->path_id);
      if ((collective_path2->src_is_endpointA && (path_desc2->thread_idB == thread_id)) || ((!collective_path2->src_is_endpointA) && (path_desc2->thread_idA == thread_id))) {
        int thread_id2 = (collective_path2->src_is_endpointA) ? path_desc2->thread_idB: path_desc2->thread_idA;
        if (thread_id == thread_id2) {
          TakyonPath *path = (collective_path2->src_is_endpointA) ? path_desc2->pathB: path_desc2->pathA;
          scatter_dest = takyonScatterDestInit(collective_desc->num_paths, j, path);
          break;
        }
      }
    }
  }
  return scatter_dest;
}

TakyonGatherSrc *takyonGetGatherSrc(TakyonGraph *graph, const char *name, int thread_id) {
  TakyonGatherSrc *gather_src = NULL;
  TakyonCollectiveGroup *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_GATHER)) {
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path2 = &collective_desc->path_list[j];
      TakyonConnection *path_desc2 = getPath(graph, collective_path2->path_id);
      if ((collective_path2->src_is_endpointA && (path_desc2->thread_idA == thread_id)) || ((!collective_path2->src_is_endpointA) && (path_desc2->thread_idB == thread_id))) {
        int thread_id2 = (collective_path2->src_is_endpointA) ? path_desc2->thread_idA: path_desc2->thread_idB;
        if (thread_id == thread_id2) {
          TakyonPath *path = (collective_path2->src_is_endpointA) ? path_desc2->pathA: path_desc2->pathB;
          gather_src = takyonGatherSrcInit(collective_desc->num_paths, j, path);
          break;
        }
      }
    }
  }
  return gather_src;
}

TakyonGatherDest *takyonGetGatherDest(TakyonGraph *graph, const char *name, int thread_id) {
  TakyonGatherDest *gather_dest = NULL;
  TakyonCollectiveGroup *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_GATHER)) {
    TakyonCollectiveConnection *collective_path = &collective_desc->path_list[0];
    TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
    if ((collective_path->src_is_endpointA && (path_desc->thread_idB == thread_id)) || ((!collective_path->src_is_endpointA) && (path_desc->thread_idA == thread_id))) {
      // This is the gather destination
      TakyonPath **path_list = (TakyonPath **)malloc(collective_desc->num_paths * sizeof(TakyonPath *));
      validateAllocation(path_list, __FUNCTION__);
      for (int j=0; j<collective_desc->num_paths; j++) {
        TakyonCollectiveConnection *collective_path2 = &collective_desc->path_list[j];
        TakyonConnection *path_desc2 = getPath(graph, collective_path2->path_id);
        path_list[j] = (collective_path->src_is_endpointA) ? path_desc2->pathB: path_desc2->pathA;
      }
      gather_dest = takyonGatherDestInit(collective_desc->num_paths, path_list);
      free(path_list);
    }
  }
  return gather_dest;
}

void takyonPrintGraph(TakyonGraph *graph) {
  printf("  Threads:\n");
  for (int i=0; i<graph->thread_group_count; i++) {
    TakyonThreadGroup *thread_group_desc = &graph->thread_group_list[i];
    printf("    %s: instances=%d\n", thread_group_desc->name, thread_group_desc->instances);
  }

  printf("  Processes:\n");
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    printf("    Process %d: Threads:", process_desc->id);
    for (int j=0; j<process_desc->thread_count; j++) {
      TakyonThreadGroup *thread_group_desc = takyonGetThreadGroup(graph, process_desc->thread_list[j].id);
      int instance = process_desc->thread_list[j].id - thread_group_desc->starting_thread_id;
      printf(" %s[%d]", thread_group_desc->name, instance);
    }
    printf("\n");
    for (int j=0; j<process_desc->memory_block_count; j++) {
      TakyonMemoryBlock *memory_block = &process_desc->memory_block_list[j];
      printf("      MemBlock '%s': where='%s', bytes=%lld, addr=0x%llx\n", memory_block->name, memory_block->where, (unsigned long long)memory_block->bytes, (unsigned long long)memory_block->addr);
    }
  }

  printf("  Paths:\n");
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    TakyonThreadGroup *thread_group_descA = takyonGetThreadGroup(graph, path_desc->thread_idA);
    int instanceA = path_desc->thread_idA - thread_group_descA->starting_thread_id;
    TakyonThreadGroup *thread_group_descB = takyonGetThreadGroup(graph, path_desc->thread_idB);
    int instanceB = path_desc->thread_idB - thread_group_descB->starting_thread_id;
    printf("    Path %d: %s[%d] <--> %s[%d]\n", path_desc->id, thread_group_descA->name, instanceA, thread_group_descB->name, instanceB);
  }

  printf("  Collectives:\n");
  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollectiveGroup *collective_desc = &graph->collective_list[i];
    printf("    '%s', Type=%s, Paths:", collective_desc->name, collectiveTypeToName(collective_desc->type));
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *path = &collective_desc->path_list[j];
      printf(" %d:%s", path->path_id, path->src_is_endpointA ? "A->B" : "B->A");
    }
    printf("\n");
  }
}
