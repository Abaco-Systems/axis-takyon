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

#include "takyon_utils.h"
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

static CollectiveDesc *takyonGetCollective(TakyonDataflow *dataflow, const char *name) {
  for (int i=0; i<dataflow->collective_count; i++) {
    CollectiveDesc *collective_desc = &dataflow->collective_list[i];
    if (strcmp(collective_desc->name, name) == 0) return collective_desc;
  }
  return NULL;
}

static const char *collectiveTypeToName(CollectiveType type) {
  if (type == COLLECTIVE_SCATTER) return "SCATTER";
  if (type == COLLECTIVE_GATHER) return "GATHER";
  return "Unknown";
}

TaskDesc *takyonGetTask(TakyonDataflow *dataflow, int thread_id) {
  for (int i=0; i<dataflow->task_count; i++) {
    TaskDesc *task_desc = &dataflow->task_list[i];
    for (int j=0; j<task_desc->thread_count; j++) {
      if (task_desc->thread_id_list[j] == thread_id) {
        return task_desc;
      }
    }
  }
  return NULL;
}

int takyonGetTaskInstance(TakyonDataflow *dataflow, int thread_id) {
  TaskDesc *task_desc = takyonGetTask(dataflow, thread_id);
  for (int i=0; i<task_desc->thread_count; i++) {
    if (task_desc->thread_id_list[i] == thread_id) {
      return i;
    }
  }
  return -1;
}

static ProcessDesc *getProcess(TakyonDataflow *dataflow, int process_id) {
  for (int i=0; i<dataflow->process_count; i++) {
    ProcessDesc *process_desc = &dataflow->process_list[i];
    if (process_desc->id == process_id) {
      return process_desc;
    }
  }
  return NULL;
}

static MemoryBlockDesc *getMemoryBlock(TakyonDataflow *dataflow, const char *mem_name) {
  for (int i=0; i<dataflow->process_count; i++) {
    ProcessDesc *process_desc = &dataflow->process_list[i];
    for (int j=0; j<process_desc->memory_block_count; j++) {
      MemoryBlockDesc *memory_block = &process_desc->memory_block_list[j];
      if (strcmp(memory_block->name, mem_name) == 0) {
        return memory_block;
      }
    }
  }
  return NULL;
}

static PathDesc *getPath(TakyonDataflow *dataflow, int path_id) {
  for (int i=0; i<dataflow->path_count; i++) {
    PathDesc *path_desc = &dataflow->path_list[i];
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

static void getCollectivePathList(TakyonDataflow *dataflow, char *data, int *count_ret, CollectivePath **collective_path_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  CollectivePath *collective_path_list = NULL;
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
      PathDesc *path_desc = getPath(dataflow, path_id);
      if (path_desc == NULL) {
        fprintf(stderr, "Line %d: There is no path for path ID=%d\n", line_count, path_id);
        exit(EXIT_FAILURE);
      }
      count++;
      collective_path_list = realloc(collective_path_list, count * sizeof(CollectivePath));
      validateAllocation(collective_path_list, __FUNCTION__);
      collective_path_list[count-1].path_id = path_id;
      collective_path_list[count-1].src_is_endpointA = (endpoint == 'A');
      strcpy(data, value);
    }
  }
  *count_ret = count;
  *collective_path_list_ret = collective_path_list;
}

static void getIntList(char *data, int *count_ret, int **int_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  int *int_list = NULL;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      int number = getIntValue(keyword, line_count, 0);
      count++;
      int_list = realloc(int_list, count * sizeof(int));
      validateAllocation(int_list, __FUNCTION__);
      int_list[count-1] = number;
      strcpy(data, value);
    }
  }
  *count_ret = count;
  *int_list_ret = int_list;
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

static void getAddrList(TakyonDataflow *dataflow, char *data, int *count_ret, size_t **addr_list_ret, int line_count) {
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
        MemoryBlockDesc *memory_block = getMemoryBlock(dataflow, mem_name);
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

static char *parseTasks(TakyonDataflow *dataflow, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKey("Tasks", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("Task:", data_ptr, keyword, value, line_count);
  while (strcmp("Task:", keyword) == 0) {
    // Alloc list item
    dataflow->task_count++;
    dataflow->task_list = realloc(dataflow->task_list, dataflow->task_count * sizeof(TaskDesc));
    validateAllocation(dataflow->task_list, __FUNCTION__);
    TaskDesc *task_desc = &dataflow->task_list[dataflow->task_count - 1];

    // Name
    validateKeyValue("Task:", data_ptr, keyword, value, line_count);
    task_desc->name = strdup(value);
    validateAllocation(task_desc->name, __FUNCTION__);
    for (int j=0; j<(dataflow->task_count-1); j++) {
      validateUniqueNames(task_desc->name, dataflow->task_list[j].name, line_count);
    }

    // ThreadIds
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("ThreadIds:", data_ptr, keyword, value, line_count);
    getIntList(value, &task_desc->thread_count, &task_desc->thread_id_list, line_count);
    // Make sure no duplicates with other lists
    for (int j=0; j<(dataflow->task_count-1); j++) {
      TaskDesc *task_desc2 = &dataflow->task_list[j];
      for (int k=0; k<task_desc2->thread_count; k++) {
        for (int l=0; l<task_desc->thread_count; l++) {
          validateUniqueInt(task_desc->thread_id_list[l], task_desc2->thread_id_list[k], line_count);
        }
      }
    }

    // Make sure no duplicates in this list
    for (int j=0; j<task_desc->thread_count; j++) {
      for (int k=j+1; k<task_desc->thread_count; k++) {
        validateUniqueInt(task_desc->thread_id_list[j], task_desc->thread_id_list[k], line_count);
      }
    }

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static char *parseProcesses(TakyonDataflow *dataflow, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("Processes", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("Process:", data_ptr, keyword, value, line_count);
  while (strcmp("Process:", keyword) == 0) {
    // Alloc list item
    dataflow->process_count++;
    dataflow->process_list = realloc(dataflow->process_list, dataflow->process_count * sizeof(ProcessDesc));
    validateAllocation(dataflow->process_list, __FUNCTION__);
    ProcessDesc *process_desc = &dataflow->process_list[dataflow->process_count - 1];
    process_desc->memory_block_count = 0;
    process_desc->memory_block_list = NULL;

    // ID
    validateKeyValue("Process:", data_ptr, keyword, value, line_count);
    process_desc->id = getIntValue(value, line_count, 0);
    for (int j=0; j<(dataflow->process_count-1); j++) {
      validateUniqueInt(process_desc->id, dataflow->process_list[j].id, line_count);
    }

    // ThreadIds
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("ThreadIds:", data_ptr, keyword, value, line_count);
    int *thread_id_list = NULL;
    getIntList(value, &process_desc->thread_count, &thread_id_list, line_count);
    // Make sure no duplicates in this list
    for (int j=0; j<process_desc->thread_count; j++) {
      for (int k=j+1; k<process_desc->thread_count; k++) {
        validateUniqueInt(thread_id_list[j], thread_id_list[k], line_count);
      }
    }
    // Make sure no duplicates in other process lists
    for (int j=0; j<(dataflow->process_count-1); j++) {
      ProcessDesc *process_desc2 = &dataflow->process_list[j];
      for (int k=0; k<process_desc2->thread_count; k++) {
        for (int l=0; l<process_desc->thread_count; l++) {
          validateUniqueInt(thread_id_list[l], process_desc2->thread_list[k].id, line_count);
        }
      }
    }

    // Build process list
    process_desc->thread_list = (ThreadDesc *)calloc(process_desc->thread_count, sizeof(ThreadDesc));
    validateAllocation(process_desc->thread_list, __FUNCTION__);
    for (int j=0; j<process_desc->thread_count; j++) {
      int thread_id = thread_id_list[j];
      TaskDesc *task_desc = takyonGetTask(dataflow, thread_id);
      if (task_desc == NULL) { fprintf(stderr, "Line %d: Thread ID=%d not valid\n", line_count, thread_id); exit(EXIT_FAILURE); }
      ThreadDesc *thread_desc = &process_desc->thread_list[j];
      thread_desc->id = thread_id;
    }
    free(thread_id_list);

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static char *parseMemoryBlocks(int my_process_id, TakyonDataflow *dataflow, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("MemoryBlocks", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while (strcmp("MemoryBlock:", keyword) == 0) {
    // Name
    validateKeyValue("MemoryBlock:", data_ptr, keyword, value, line_count);
    char *name = strdup(value);
    validateAllocation(name, __FUNCTION__);
    // Make sure the name is unique across all processes
    for (int j=0; j<dataflow->process_count; j++) {
      ProcessDesc *process_desc = &dataflow->process_list[j];
      for (int k=0; k<process_desc->memory_block_count; k++) {
        validateUniqueNames(name, process_desc->memory_block_list[k].name, line_count);
      }
    }

    // Process ID
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("ProcessId:", data_ptr, keyword, value, line_count);
    int process_id = getIntValue(value, line_count, 0);
    ProcessDesc *process_desc = getProcess(dataflow, process_id);
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
    process_desc->memory_block_list = realloc(process_desc->memory_block_list, process_desc->memory_block_count * sizeof(MemoryBlockDesc));
    validateAllocation(process_desc->memory_block_list, __FUNCTION__);
    MemoryBlockDesc *memory_block = &process_desc->memory_block_list[process_desc->memory_block_count - 1];
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

static CollectiveType getCollectiveTypeFromText(const char *value, int line_count) {
  if (strcmp(value, "SCATTER") == 0) return COLLECTIVE_SCATTER;
  if (strcmp(value, "GATHER") == 0) return COLLECTIVE_GATHER;
  fprintf(stderr, "Line %d: The name '%s' is not a valid collective type.\n", line_count, value);
  exit(EXIT_FAILURE);
}

static char *parseCollectiveGroups(TakyonDataflow *dataflow, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("CollectiveGroups", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while (strcmp("CollectiveGroup:", keyword) == 0) {
    // Alloc list item
    dataflow->collective_count++;
    dataflow->collective_list = realloc(dataflow->collective_list, dataflow->collective_count * sizeof(CollectiveDesc));
    validateAllocation(dataflow->collective_list, __FUNCTION__);
    CollectiveDesc *collective_desc = &dataflow->collective_list[dataflow->collective_count - 1];
    memset(collective_desc, 0, sizeof(CollectiveDesc));

    // Name
    validateKeyValue("CollectiveGroup:", data_ptr, keyword, value, line_count);
    collective_desc->name = strdup(value);
    validateAllocation(collective_desc->name, __FUNCTION__);
    for (int j=0; j<(dataflow->collective_count-1); j++) {
      validateUniqueNames(collective_desc->name, dataflow->collective_list[j].name, line_count);
    }

    // Type
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Type:", data_ptr, keyword, value, line_count);
    collective_desc->type = getCollectiveTypeFromText(value, line_count);

    // Get path list: <path_id>:{A | B}
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("PathSrcIds:", data_ptr, keyword, value, line_count);
    getCollectivePathList(dataflow, value, &collective_desc->num_paths, &collective_desc->path_list, line_count);

    // Verify all paths are unique
    for (int i=0; i<collective_desc->num_paths; i++) {
      for (int j=i+1; j<collective_desc->num_paths; j++) {
        CollectivePath *coll_path1 = &collective_desc->path_list[i];
        CollectivePath *coll_path2 = &collective_desc->path_list[j];
        if (coll_path1->path_id == coll_path2->path_id) {
          fprintf(stderr, "Path ID=%d is defined multiple times in the collective group '%s'\n", coll_path1->path_id, collective_desc->name);
          exit(EXIT_FAILURE);
        }
      }
    }

    // Validate collective connectivity
    if (collective_desc->type == COLLECTIVE_SCATTER) {
      // Make sure all scatter sources are the same
      CollectivePath *first_coll_path = &collective_desc->path_list[0];
      PathDesc *first_path_desc = getPath(dataflow, first_coll_path->path_id);
      int src_thread_id = first_coll_path->src_is_endpointA ? first_path_desc->thread_idA : first_path_desc->thread_idB;
      for (int i=1; i<collective_desc->num_paths; i++) {
        CollectivePath *coll_path = &collective_desc->path_list[i];
        PathDesc *path_desc = getPath(dataflow, coll_path->path_id);
        int thread_id = coll_path->src_is_endpointA ? path_desc->thread_idA : path_desc->thread_idB;
        if (thread_id != src_thread_id) {
          fprintf(stderr, "Collective group '%s' is a scatter, but the source thread IDs are not all the same.\n", collective_desc->name);
          exit(EXIT_FAILURE);
        }
      }
    } else if (collective_desc->type == COLLECTIVE_GATHER) {
      // Make sure all gather destinations are the same
      CollectivePath *first_coll_path = &collective_desc->path_list[0];
      PathDesc *first_path_desc = getPath(dataflow, first_coll_path->path_id);
      int dest_thread_id = first_coll_path->src_is_endpointA ? first_path_desc->thread_idB : first_path_desc->thread_idA;
      for (int i=1; i<collective_desc->num_paths; i++) {
        CollectivePath *coll_path = &collective_desc->path_list[i];
        PathDesc *path_desc = getPath(dataflow, coll_path->path_id);
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

static void updateParam(TakyonDataflow *dataflow, PathParam *param, char *value, int line_count) {
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
    getAddrList(dataflow, valueA, &param->listA_count, &param->addr_listA, line_count);
    getAddrList(dataflow, valueB, &param->listB_count, &param->addr_listB, line_count);
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

static char *parsePaths(TakyonDataflow *dataflow, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
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
        updateParam(dataflow, &default_params[param_index], value, line_count);
        // Get next line
        data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
        param_index = getParamIndex(keyword, default_params, NUM_DEFAULT_PARAMS);
      }
    } else {
      // Alloc list item
      dataflow->path_count++;
      dataflow->path_list = realloc(dataflow->path_list, dataflow->path_count * sizeof(PathDesc));
      validateAllocation(dataflow->path_list, __FUNCTION__);
      PathDesc *path_desc = &dataflow->path_list[dataflow->path_count - 1];
      path_desc->pathA = NULL;
      path_desc->pathB = NULL;

      // ID
      validateKeyValue("Path:", data_ptr, keyword, value, line_count);
      path_desc->id = getIntValue(value, line_count, 0);
      for (int j=0; j<(dataflow->path_count-1); j++) {
        validateUniqueInt(path_desc->id, dataflow->path_list[j].id, line_count);
      }

      // Path attributes
      PathParam path_params[NUM_PATH_PARAMS];
      memset(path_params, 0, NUM_PATH_PARAMS*sizeof(PathParam));
      static char interconnectA[MAX_TAKYON_INTERCONNECT_CHARS];
      static char interconnectB[MAX_TAKYON_INTERCONNECT_CHARS];
      copyParams(path_params, default_params, NUM_DEFAULT_PARAMS);
      setParamDefaultValueInt(&path_params[NUM_DEFAULT_PARAMS], "ThreadId:", -1);
      interconnectA[0] = '\0';
      interconnectB[0] = '\0';

      // Get path attributes
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      int param_index = getParamIndex(keyword, path_params, NUM_PATH_PARAMS);
      while ((param_index >= 0) || (strcmp(keyword,"InterconnectA:")==0) || (strcmp(keyword,"InterconnectB:")==0)) {
        if (param_index >= 0) {
          // Update path params
          updateParam(dataflow, &path_params[param_index], value, line_count);
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

      // Validate endpoint A buffer counts
      param_index = getParamIndex("NBufsAtoB:", path_params, NUM_PATH_PARAMS);
      nbufs_AtoB = path_params[param_index].intB;
      param_index = getParamIndex("NBufsBtoA:", path_params, NUM_PATH_PARAMS);
      nbufs_BtoA = path_params[param_index].intB;
      param_index = getParamIndex("SenderMaxBytesList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_AtoB != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsAtoB'=%d for endpointB does not match the number of values for 'SenderMaxBytesList'\n", line_count-1, nbufs_AtoB);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("RecverMaxBytesList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_BtoA != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsBtoA'=%d for endpointB does not match the number of values for 'RecverMaxBytesList'\n", line_count-1, nbufs_BtoA);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("SenderAddrList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_AtoB != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsAtoB'=%d for endpointB does not match the number of values for 'SenderAddrList'\n", line_count-1, nbufs_AtoB);
        exit(EXIT_FAILURE);
      }
      param_index = getParamIndex("RecverAddrList:", path_params, NUM_PATH_PARAMS);
      if (nbufs_BtoA != path_params[param_index].listB_count) {
        fprintf(stderr, "Line %d: 'NBufsBtoA'=%d for endpointB does not match the number of values for 'RecverAddrList'\n", line_count-1, nbufs_BtoA);
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

TakyonDataflow *takyonLoadDataflowDescription(int process_id, const char *filename) {
  char *file_data = loadFile(filename);
  char *data_ptr = file_data;
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int line_count = 1;

  TakyonDataflow *dataflow = (TakyonDataflow *)calloc(1, sizeof(TakyonDataflow));
  validateAllocation(dataflow, __FUNCTION__);

  // App name
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("App:", data_ptr, keyword, value, line_count);
  dataflow->app_name = strdup(value);
  validateAllocation(dataflow->app_name, __FUNCTION__);
#ifdef DEBUG_MESSAGE
  printf("App name: %s\n", dataflow->app_name);
#endif
  
  // Tasks
  data_ptr = parseTasks(dataflow, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Tasks = %d\n", dataflow->task_count);
  for (int i=0; i<dataflow->task_count; i++) {
    TaskDesc *task_desc = &dataflow->task_list[i];
    printf("  %s:", task_desc->name);
    for (int j=0; j<task_desc->thread_count; j++) {
      printf(" %d", task_desc->thread_id_list[j]);
    }
    printf("\n");
  }
#endif

  // Processes
  data_ptr = parseProcesses(dataflow, data_ptr, keyword, value, &line_count);
  // MemoryBlocks (get mapped to a process)
  data_ptr = parseMemoryBlocks(process_id, dataflow, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Processes = %d\n", dataflow->process_count);
  for (int i=0; i<dataflow->process_count; i++) {
    ProcessDesc *process_desc = &dataflow->process_list[i];
    printf("  %d:", process_desc->id);
    for (int j=0; j<process_desc->thread_count; j++) {
      printf(" %d", process_desc->thread_list[j].id);
    }
    printf("\n");
    for (int j=0; j<process_desc->memory_block_count; j++) {
      MemoryBlockDesc *memory_block = &process_desc->memory_block_list[j];
      printf("    MemBlock %d: where='%s', bytes=%lld, addr=0x%llx\n", memory_block->id, memory_block->where, (unsigned long long)memory_block->bytes, (unsigned long long)memory_block->addr);
    }
  }
#endif

  // Paths
  data_ptr = parsePaths(dataflow, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Paths = %d\n", dataflow->path_count);
  for (int i=0; i<dataflow->path_count; i++) {
    PathDesc *path_desc = &dataflow->path_list[i];
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
  data_ptr = parseCollectiveGroups(dataflow, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Collectives = %d\n", dataflow->collective_count);
  for (int i=0; i<dataflow->collective_count; i++) {
    CollectiveDesc *collective_desc = &dataflow->collective_list[i];
    printf("  '%s', Type=%s, Paths:", collective_desc->name, collectiveTypeToName(collective_desc->type));
    for (int j=0; j<collective_desc->num_paths; j++) {
      CollectivePath *path = &collective_desc->path_list[j];
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

  return dataflow;
}

void takyonFreeDataflowDescription(TakyonDataflow *dataflow, int my_process_id) {
  free(dataflow->app_name);

  for (int i=0; i<dataflow->task_count; i++) {
    free(dataflow->task_list[i].name);
    free(dataflow->task_list[i].thread_id_list);
  }
  free(dataflow->task_list);

  for (int i=0; i<dataflow->process_count; i++) {
    ProcessDesc *process = &dataflow->process_list[i];
    free(process->thread_list);
    for (int j=0; j<process->memory_block_count; j++) {
      MemoryBlockDesc *block = &process->memory_block_list[j];
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
  free(dataflow->process_list);

  for (int i=0; i<dataflow->path_count; i++) {
    PathDesc *path_desc = &dataflow->path_list[i];
    takyonFreeAttributes(path_desc->attrsA);
    takyonFreeAttributes(path_desc->attrsB);
  }
  free(dataflow->path_list);

  for (int i=0; i<dataflow->collective_count; i++) {
    CollectiveDesc *collective = &dataflow->collective_list[i];
    free(collective->name);
    free(collective->path_list);
  }
  if (dataflow->collective_count > 0) free(dataflow->collective_list);

  free(dataflow);
}

void takyonCreateDataflowPaths(TakyonDataflow *dataflow, int thread_id) {
  // Create all the paths
  for (int i=0; i<dataflow->path_count; i++) {
    PathDesc *path_desc = &dataflow->path_list[i];
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

void takyonDestroyDataflowPaths(TakyonDataflow *dataflow, int thread_id) {
  for (int i=0; i<dataflow->path_count; i++) {
    PathDesc *path_desc = &dataflow->path_list[i];
    if (path_desc->thread_idA == thread_id || path_desc->thread_idB == thread_id) {
      TakyonPath *path = (path_desc->thread_idA == thread_id) ? path_desc->pathA : path_desc->pathB;
      char *error_message = takyonDestroy(&path);
      if (error_message != NULL) {
        fprintf(stderr, "%s(): Failed to destroy path: %s\n", __FUNCTION__, error_message);
        free(error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

ScatterSrc *takyonGetScatterSrc(TakyonDataflow *dataflow, const char *name, int thread_id) {
  ScatterSrc *scatter_src = NULL;
  CollectiveDesc *collective_desc = takyonGetCollective(dataflow, name);
  if ((collective_desc != NULL) && (collective_desc->type == COLLECTIVE_SCATTER)) {
    CollectivePath *collective_path = &collective_desc->path_list[0];
    PathDesc *path_desc = getPath(dataflow, collective_path->path_id);
    if ((collective_path->src_is_endpointA && (path_desc->thread_idA == thread_id)) || ((!collective_path->src_is_endpointA) && (path_desc->thread_idB == thread_id))) {
      // This is the scatter source
      TakyonPath **path_list = (TakyonPath **)malloc(collective_desc->num_paths * sizeof(TakyonPath *));
      validateAllocation(path_list, __FUNCTION__);
      for (int j=0; j<collective_desc->num_paths; j++) {
        CollectivePath *collective_path2 = &collective_desc->path_list[j];
        PathDesc *path_desc2 = getPath(dataflow, collective_path2->path_id);
        path_list[j] = (collective_path->src_is_endpointA) ? path_desc2->pathA: path_desc2->pathB;
      }
      scatter_src = takyonScatterSrcInit(collective_desc->num_paths, path_list);
      free(path_list);
    }
  }
  return scatter_src;
}

ScatterDest *takyonGetScatterDest(TakyonDataflow *dataflow, const char *name, int thread_id) {
  ScatterDest *scatter_dest = NULL;
  CollectiveDesc *collective_desc = takyonGetCollective(dataflow, name);
  if ((collective_desc != NULL) && (collective_desc->type == COLLECTIVE_SCATTER)) {
    for (int j=0; j<collective_desc->num_paths; j++) {
      CollectivePath *collective_path2 = &collective_desc->path_list[j];
      PathDesc *path_desc2 = getPath(dataflow, collective_path2->path_id);
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

GatherSrc *takyonGetGatherSrc(TakyonDataflow *dataflow, const char *name, int thread_id) {
  GatherSrc *gather_src = NULL;
  CollectiveDesc *collective_desc = takyonGetCollective(dataflow, name);
  if ((collective_desc != NULL) && (collective_desc->type == COLLECTIVE_GATHER)) {
    for (int j=0; j<collective_desc->num_paths; j++) {
      CollectivePath *collective_path2 = &collective_desc->path_list[j];
      PathDesc *path_desc2 = getPath(dataflow, collective_path2->path_id);
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

GatherDest *takyonGetGatherDest(TakyonDataflow *dataflow, const char *name, int thread_id) {
  GatherDest *gather_dest = NULL;
  CollectiveDesc *collective_desc = takyonGetCollective(dataflow, name);
  if ((collective_desc != NULL) && (collective_desc->type == COLLECTIVE_GATHER)) {
    CollectivePath *collective_path = &collective_desc->path_list[0];
    PathDesc *path_desc = getPath(dataflow, collective_path->path_id);
    if ((collective_path->src_is_endpointA && (path_desc->thread_idB == thread_id)) || ((!collective_path->src_is_endpointA) && (path_desc->thread_idA == thread_id))) {
      // This is the gather destination
      TakyonPath **path_list = (TakyonPath **)malloc(collective_desc->num_paths * sizeof(TakyonPath *));
      validateAllocation(path_list, __FUNCTION__);
      for (int j=0; j<collective_desc->num_paths; j++) {
        CollectivePath *collective_path2 = &collective_desc->path_list[j];
        PathDesc *path_desc2 = getPath(dataflow, collective_path2->path_id);
        path_list[j] = (collective_path->src_is_endpointA) ? path_desc2->pathB: path_desc2->pathA;
      }
      gather_dest = takyonGatherDestInit(collective_desc->num_paths, path_list);
      free(path_list);
    }
  }
  return gather_dest;
}

void takyonPrintDataflow(TakyonDataflow *dataflow) {
  printf("  App: %s\n", dataflow->app_name);

  printf("  Threads:\n");
  for (int i=0; i<dataflow->task_count; i++) {
    TaskDesc *task_desc = &dataflow->task_list[i];
    printf("    %s:", task_desc->name);
    for (int j=0; j<task_desc->thread_count; j++) {
      printf(" %d", task_desc->thread_id_list[j]);
    }
    printf("\n");
  }

  printf("  Processes:\n");
  for (int i=0; i<dataflow->process_count; i++) {
    ProcessDesc *process_desc = &dataflow->process_list[i];
    printf("    Process %d: Threads=(", process_desc->id);
    for (int j=0; j<process_desc->thread_count; j++) {
      printf(" %d", process_desc->thread_list[j].id);
    }
    printf(" )\n");
    for (int j=0; j<process_desc->memory_block_count; j++) {
      MemoryBlockDesc *memory_block = &process_desc->memory_block_list[j];
      printf("      MemBlock '%s': where='%s', bytes=%lld, addr=0x%llx\n", memory_block->name, memory_block->where, (unsigned long long)memory_block->bytes, (unsigned long long)memory_block->addr);
    }
  }

  printf("  Paths:\n");
  for (int i=0; i<dataflow->path_count; i++) {
    PathDesc *path_desc = &dataflow->path_list[i];
    printf("    Path %d: (Thread %d <--> Thread %d)\n", path_desc->id, path_desc->thread_idA, path_desc->thread_idB);
  }

  printf("  Collectives:\n");
  for (int i=0; i<dataflow->collective_count; i++) {
    CollectiveDesc *collective_desc = &dataflow->collective_list[i];
    printf("    '%s', Type=%s, Paths:", collective_desc->name, collectiveTypeToName(collective_desc->type));
    for (int j=0; j<collective_desc->num_paths; j++) {
      CollectivePath *path = &collective_desc->path_list[j];
      printf(" %d:%s", path->path_id, path->src_is_endpointA ? "A->B" : "B->A");
    }
    printf("\n");
  }
}
