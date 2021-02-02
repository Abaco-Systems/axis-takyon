// Copyright 2018,2020 Abaco Systems
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

static int getTotalGroupIdCount(TakyonGraph *graph) {
  TakyonGroup *last_group = &graph->group_list[graph->group_count-1];
  int total_ids = last_group->starting_group_id + last_group->instances;
  return total_ids;
}

static TakyonCollective *takyonGetCollective(TakyonGraph *graph, const char *name) {
  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollective *collective_desc = &graph->collective_list[i];
    if (strcmp(collective_desc->name, name) == 0) return collective_desc;
  }
  return NULL;
}

static const char *collectiveTypeToName(TakyonCollectiveType type) {
  if (type == TAKYON_COLLECTIVE_BARRIER) return "BARRIER";
  if (type == TAKYON_COLLECTIVE_REDUCE) return "REDUCE";
  if (type == TAKYON_COLLECTIVE_ONE2ONE) return "ONE2ONE";
  if (type == TAKYON_COLLECTIVE_SCATTER) return "SCATTER";
  if (type == TAKYON_COLLECTIVE_GATHER) return "GATHER";
  return "Unknown";
}

TakyonGroup *takyonGetGroupByName(TakyonGraph *graph, const char *name) {
  for (int i=0; i<graph->group_count; i++) {
    TakyonGroup *group_desc = &graph->group_list[i];
    if (strcmp(group_desc->name, name)==0) {
      return group_desc;
    }
  }
  return NULL;
}

TakyonGroup *takyonGetGroup(TakyonGraph *graph, int group_id) {
  for (int i=0; i<graph->group_count; i++) {
    TakyonGroup *group_desc = &graph->group_list[i];
    int ending_group_id = group_desc->starting_group_id + (group_desc->instances - 1);
    if ((group_id >= group_desc->starting_group_id) && (group_id <= ending_group_id)) {
      return group_desc;
    }
  }
  return NULL;
}

int takyonGetGroupInstance(TakyonGraph *graph, int group_id) {
  TakyonGroup *group_desc = takyonGetGroup(graph, group_id);
  if (group_desc != NULL) {
    return group_id - group_desc->starting_group_id;
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

static TakyonBuffer *getBuffer(TakyonGraph *graph, const char *mem_name) {
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    for (int j=0; j<process_desc->buffer_count; j++) {
      TakyonBuffer *buffer = &process_desc->buffer_list[j];
      if (strcmp(buffer->name, mem_name) == 0) {
        return buffer;
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
  uint64_t number;
  int tokens = sscanf(data, "%ju", &number);
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
    else if (strcmp(token, "TAKYON_VERBOSITY_CREATE_DESTROY") == 0) verbosity |= TAKYON_VERBOSITY_CREATE_DESTROY;
    else if (strcmp(token, "TAKYON_VERBOSITY_CREATE_DESTROY_MORE") == 0) verbosity |= TAKYON_VERBOSITY_CREATE_DESTROY_MORE;
    else if (strcmp(token, "TAKYON_VERBOSITY_SEND_RECV") == 0) verbosity |= TAKYON_VERBOSITY_SEND_RECV;
    else if (strcmp(token, "TAKYON_VERBOSITY_SEND_RECV_MORE") == 0) verbosity |= TAKYON_VERBOSITY_SEND_RECV_MORE;
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
  } else if (strcmp(data, "TAKYON_USE_IS_SEND_FINISHED")==0) {
    return TAKYON_USE_IS_SEND_FINISHED;
  } else {
    fprintf(stderr, "Line %d: Expected 'TAKYON_BLOCKING' or 'TAKYON_USE_IS_SEND_FINISHED' but got '%s'\n", line_count, data);
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

static void validateTreeChildren(TakyonPathTree *current_path_tree, TakyonCollectiveConnection *path_list, TakyonGraph *graph, int line_count) {
  // Verify all children sources are the same group ID
  int path_index1 = current_path_tree->children[0].path_index;
  TakyonCollectiveConnection *path1 = &path_list[path_index1];
  int path_id1 = path1->path_id;
  TakyonConnection *connection1 = getPath(graph, path_id1);
  int group_id1 = path1->src_is_endpointA ? connection1->group_idA : connection1->group_idB;
  for (int i=1; i<current_path_tree->num_children; i++) {
    int path_index2 = current_path_tree->children[i].path_index;
    TakyonCollectiveConnection *path2 = &path_list[path_index2];
    int path_id2 = path2->path_id;
    TakyonConnection *connection2 = getPath(graph, path_id2);
    int group_id2 = path2->src_is_endpointA ? connection2->group_idA : connection2->group_idB;
    if (group_id2 != group_id1) {
      fprintf(stderr, "Line %d: The path tree has siblings that do not have the same source group ID\n", line_count);
      exit(EXIT_FAILURE);
    }
  }
}

static void getCollectivePathTree(TakyonGraph *graph, char *data, int *count_ret, TakyonCollectiveConnection **collective_path_list_ret, TakyonPathTree **path_tree_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  TakyonCollectiveConnection *collective_path_list = NULL;
  TakyonPathTree *top_path_tree = calloc(1, sizeof(TakyonPathTree));
  validateAllocation(top_path_tree, __FUNCTION__);
  TakyonPathTree *current_path_tree = top_path_tree;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      if (strcmp(keyword, "(")== 0) {
        // New tree level
        if (current_path_tree->num_children == 0) {
          fprintf(stderr, "Line %d: The format for the node tree should NOT start with '(' or have multiple '(' in a row.\n", line_count);
          exit(EXIT_FAILURE);
        }
        current_path_tree = &current_path_tree->children[current_path_tree->num_children-1];
      } else if (strcmp(keyword, ")")== 0) {
        // Reverse up the tree
        if (current_path_tree->num_children == 0) {
          fprintf(stderr, "Line %d: The format for the node tree should NOT contain '( )' with no contents.\n", line_count);
          exit(EXIT_FAILURE);
        }
        validateTreeChildren(current_path_tree, collective_path_list, graph, line_count);
        if (current_path_tree->parent == NULL) {
          fprintf(stderr, "Line %d: The format for the node tree contains too many ')'.\n", line_count);
          exit(EXIT_FAILURE);
        }
        current_path_tree = current_path_tree->parent;
      } else {
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
        // Add path to list
        count++;
        collective_path_list = realloc(collective_path_list, count * sizeof(TakyonCollectiveConnection));
        validateAllocation(collective_path_list, __FUNCTION__);
        collective_path_list[count-1].path_id = path_id;
        collective_path_list[count-1].src_is_endpointA = (endpoint == 'A');
        // Add path to tree
        current_path_tree->num_children++;
        current_path_tree->children = realloc(current_path_tree->children, current_path_tree->num_children * sizeof(TakyonPathTree));
        validateAllocation(current_path_tree->children, __FUNCTION__);
        TakyonPathTree *child = &current_path_tree->children[current_path_tree->num_children-1];
        child->path_index = count-1;
        child->num_children = 0;
        child->children = NULL;
        child->parent = current_path_tree;
      }
      strcpy(data, value);
    }
  }

  // Do some last error checking
  if (current_path_tree != top_path_tree) {
    fprintf(stderr, "Line %d: The tree list has unbalanced parenthesis\n", line_count);
    exit(EXIT_FAILURE);
  }
  validateTreeChildren(top_path_tree, collective_path_list, graph, line_count);

  *count_ret = count;
  *collective_path_list_ret = collective_path_list;
  *path_tree_ret = top_path_tree;
}

static void getUInt64List(char *data, int *count_ret, uint64_t **uint64_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  uint64_t *uint64_list = NULL;
  // NOTE: allow "-" to mean there are no values
  if (strcmp(data, "-") != 0) {
    // Valid values should exist
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
  }
  *count_ret = count;
  *uint64_list_ret = uint64_list;
}

static int getGroupIdFromInstance(TakyonGraph *graph, char *data, int line_count) {
  // See if the ID is meant to be skipped
  if (strcmp(data, "-") == 0) {
    return -1;
  }

  // Format is <group_name>[index]
  static char group_name[MAX_VALUE_BYTES];
  size_t length = strlen(data);
  size_t open_bracket_index = 0;
  for (size_t i=0; i<length; i++) {
    if (data[i] == '[') {
      open_bracket_index = i;
      group_name[i] = '\0';
      break;
    }
    group_name[i] = data[i];
  }

  int group_index;
  int tokens = sscanf(&data[open_bracket_index], "[%u]", &group_index);
  if (tokens != 1) {
    fprintf(stderr, "Line %d: '%s' is not a valid group ID. Must be '<group_name>[index]'\n", line_count, data);
    exit(EXIT_FAILURE);
  }

  TakyonGroup *group_desc = takyonGetGroupByName(graph, group_name);
  if (group_desc == NULL) {
    fprintf(stderr, "Line %d: The group ID with name='%s' was not defined in the 'Groups' section.\n", line_count, group_name);
    exit(EXIT_FAILURE);
  }
  if (group_index < 0 || group_index >= group_desc->instances) {
    fprintf(stderr, "Line %d: The group ID '%s' referneces an index that is out of range: max index allowed is %d\n", line_count, data, group_desc->instances-1);
    exit(EXIT_FAILURE);
  }

  int group_id = group_desc->starting_group_id + group_index;
  return group_id;
}

static void getGroupIDList(TakyonGraph *graph, char *data, int *count_ret, int **int_list_ret, int line_count) {
  static char keyword[MAX_KEYWORD_BYTES];
  static char value[MAX_VALUE_BYTES];
  int count = 0;
  int *int_list = NULL;
  while (data != NULL) {
    data = getNextKeyValue(data, keyword, value, &line_count);
    if (data != NULL) {
      // Check for: <group_name>[index]
      static char group_id_name[MAX_VALUE_BYTES];
      size_t length = strlen(keyword);
      size_t open_bracket_index = 0;
      for (size_t i=0; i<length; i++) {
        if (keyword[i] == '[') {
          open_bracket_index = i;
          group_id_name[i] = '\0';
          break;
        }
        group_id_name[i] = keyword[i];
      }
      int group_index;
      int tokens = sscanf(&keyword[open_bracket_index], "[%u]", &group_index);
      if (tokens != 1) {
        fprintf(stderr, "Line %d: Can't build group ID list. '%s' is not a valid group ID. Must be '<group_name>[index]'\n", line_count, keyword);
        exit(EXIT_FAILURE);
      }
      TakyonGroup *group_desc = takyonGetGroupByName(graph, group_id_name);
      if (group_desc == NULL) {
        fprintf(stderr, "Line %d: The group ID with name='%s' was not defined in the 'Groups' section.\n", line_count, group_id_name);
        exit(EXIT_FAILURE);
      }
      if (group_index < 0 && group_index >= group_desc->instances) {
        fprintf(stderr, "Line %d: The group ID '%s' referneces an index that is out of range: max index allowed is %d\n", line_count, keyword, group_desc->instances-1);
        exit(EXIT_FAILURE);
      }

      count++;
      int_list = realloc(int_list, count * sizeof(int));
      validateAllocation(int_list, __FUNCTION__);
      int_list[count-1] = group_desc->starting_group_id + group_index;
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
  // NOTE: allow "-" to mean there are no values
  if (strcmp(data, "-") != 0) {
    // Valid values should exist
    while (data != NULL) {
      data = getNextKeyValue(data, keyword, value, &line_count);
      if (data != NULL) {
        size_t number = 0;
        if (strcmp(keyword, "NULL") == 0) {
          number = 0;
        } else {
          // Check for: <buffer_name>:<offset>
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
          uint64_t offset;
          int tokens = sscanf(&keyword[colon_index], ":%ju", &offset);
          if (tokens != 1) {
            fprintf(stderr, "Line %d: '%s' is not a valid address. Must be 'NULL' or '<buffer_name>:<offset>'\n", line_count, keyword);
            exit(EXIT_FAILURE);
          }
          TakyonBuffer *buffer = getBuffer(graph, mem_name);
          if (buffer == NULL) {
            fprintf(stderr, "Line %d: The buffer with name='%s' was not defined in the 'Buffers' section.\n", line_count, mem_name);
            exit(EXIT_FAILURE);
          }
          if (offset >= buffer->bytes) {
            fprintf(stderr, "Line %d: The buffer with name='%s' does not have enough bytes for offset=%ju.\n", line_count, mem_name, offset);
            exit(EXIT_FAILURE);
          }
          number = (size_t)buffer->addr + (size_t)offset;
        }

        count++;
        addr_list = realloc(addr_list, count * sizeof(size_t));
        validateAllocation(addr_list, __FUNCTION__);
        addr_list[count-1] = number;
        strcpy(data, value);
      }
    }
  }
  *count_ret = count;
  *addr_list_ret = addr_list;
}

static char *parseGroups(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;
  int starting_group_id = 0;

  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKey("Groups", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("Group:", data_ptr, keyword, value, line_count);
  while (strcmp("Group:", keyword) == 0) {
    // Alloc list item
    graph->group_count++;
    graph->group_list = realloc(graph->group_list, graph->group_count * sizeof(TakyonGroup));
    validateAllocation(graph->group_list, __FUNCTION__);
    TakyonGroup *group_desc = &graph->group_list[graph->group_count - 1];
    group_desc->starting_group_id = starting_group_id;

    // Name
    validateKeyValue("Group:", data_ptr, keyword, value, line_count);
    group_desc->name = strdup(value);
    validateAllocation(group_desc->name, __FUNCTION__);
    for (int j=0; j<(graph->group_count-1); j++) {
      validateUniqueNames(group_desc->name, graph->group_list[j].name, line_count);
    }

    // Instances
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("Instances:", data_ptr, keyword, value, line_count);
    group_desc->instances = getIntValue(value, line_count, 1);
    starting_group_id += group_desc->instances;

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static char *parseProcesses(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;
  int total_ids_in_process = 0;

  validateKey("Processes", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  validateKeyValue("Process:", data_ptr, keyword, value, line_count);
  while (strcmp("Process:", keyword) == 0) {
    // Alloc list item
    graph->process_count++;
    graph->process_list = realloc(graph->process_list, graph->process_count * sizeof(TakyonProcess));
    validateAllocation(graph->process_list, __FUNCTION__);
    TakyonProcess *process_desc = &graph->process_list[graph->process_count - 1];
    process_desc->buffer_count = 0;
    process_desc->buffer_list = NULL;

    // ID
    validateKeyValue("Process:", data_ptr, keyword, value, line_count);
    process_desc->id = getIntValue(value, line_count, 0);
    for (int j=0; j<(graph->process_count-1); j++) {
      validateUniqueInt(process_desc->id, graph->process_list[j].id, line_count);
    }

    // GroupIDs
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
    validateKeyValue("GroupIDs:", data_ptr, keyword, value, line_count);
    int *group_id_list = NULL;
    getGroupIDList(graph, value, &process_desc->thread_count, &group_id_list, line_count);
    total_ids_in_process += process_desc->thread_count;

    // Make sure no duplicates in this list
    for (int j=0; j<process_desc->thread_count; j++) {
      for (int k=j+1; k<process_desc->thread_count; k++) {
        validateUniqueInt(group_id_list[j], group_id_list[k], line_count);
      }
    }
    // Make sure no duplicates in other process lists
    for (int j=0; j<(graph->process_count-1); j++) {
      TakyonProcess *process_desc2 = &graph->process_list[j];
      for (int k=0; k<process_desc2->thread_count; k++) {
        for (int l=0; l<process_desc->thread_count; l++) {
          validateUniqueInt(group_id_list[l], process_desc2->thread_list[k].group_id, line_count);
        }
      }
    }

    // Allocate thread desc list
    process_desc->thread_list = (TakyonThread *)calloc(process_desc->thread_count, sizeof(TakyonThread));
    validateAllocation(process_desc->thread_list, __FUNCTION__);

    // Make sure all group IDs in this list are unique
    for (int j=0; j<process_desc->thread_count; j++) {
      int group_id = group_id_list[j];
      TakyonGroup *group_desc = takyonGetGroup(graph, group_id);
      if (group_desc == NULL) { fprintf(stderr, "Line %d: group ID=%d not valid\n", line_count, group_id); exit(EXIT_FAILURE); }
      TakyonThread *thread_desc = &process_desc->thread_list[j];
      thread_desc->group_id = group_id;
    }
    free(group_id_list);

    // Get next token
    data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  }

  // Verify all group IDs are used in procesess
  int total_expected_ids = getTotalGroupIdCount(graph);
  if (total_ids_in_process != total_expected_ids) {
    fprintf(stderr, "Line %d: The processes only make use of %d of the %d defined group IDs\n", line_count, total_ids_in_process, total_expected_ids);
    exit(EXIT_FAILURE);
  }

  *line_count_ret = line_count;
  return data_ptr;
}

static char *parseBuffers(int my_process_id, TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("Buffers", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while (strcmp("Buffer:", keyword) == 0) {
    // Name
    validateKeyValue("Buffer:", data_ptr, keyword, value, line_count);
    char *name = strdup(value);
    validateAllocation(name, __FUNCTION__);
    // Make sure the name is unique across all processes
    for (int j=0; j<graph->process_count; j++) {
      TakyonProcess *process_desc = &graph->process_list[j];
      for (int k=0; k<process_desc->buffer_count; k++) {
        validateUniqueNames(name, process_desc->buffer_list[k].name, line_count);
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
    process_desc->buffer_count++;
    process_desc->buffer_list = realloc(process_desc->buffer_list, process_desc->buffer_count * sizeof(TakyonBuffer));
    validateAllocation(process_desc->buffer_list, __FUNCTION__);
    TakyonBuffer *buffer = &process_desc->buffer_list[process_desc->buffer_count - 1];
    buffer->name = name;
    buffer->where = where;
    validateAllocation(buffer->where, __FUNCTION__);
    buffer->bytes = bytes;
    buffer->addr = NULL;
    // NOTE: This function is application defined
    if (process_id == my_process_id) {
      // This is the process that allocates the memory
      buffer->addr = appAllocateMemory(buffer->name, buffer->where, buffer->bytes, &buffer->user_data);
      if (buffer->addr == NULL) {
        fprintf(stderr, "Application falied to return a valid address: takyonAllocateMemory(name='%s', where='%s', bytes=%ju)\n", buffer->name, buffer->where, buffer->bytes);
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
  if (strcmp(value, "BARRIER") == 0) return TAKYON_COLLECTIVE_BARRIER;
  if (strcmp(value, "REDUCE") == 0) return TAKYON_COLLECTIVE_REDUCE;
  if (strcmp(value, "ONE2ONE") == 0) return TAKYON_COLLECTIVE_ONE2ONE;
  if (strcmp(value, "SCATTER") == 0) return TAKYON_COLLECTIVE_SCATTER;
  if (strcmp(value, "GATHER") == 0) return TAKYON_COLLECTIVE_GATHER;
  fprintf(stderr, "Line %d: The name '%s' is not a valid collective type.\n", line_count, value);
  exit(EXIT_FAILURE);
}

static char *parseCollectives(TakyonGraph *graph, char *data_ptr, char *keyword, char *value, int *line_count_ret) {
  int line_count = *line_count_ret;

  validateKey("Collectives", data_ptr, keyword, line_count);
  data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
  while (strcmp("Collective:", keyword) == 0) {
    // Alloc list item
    graph->collective_count++;
    graph->collective_list = realloc(graph->collective_list, graph->collective_count * sizeof(TakyonCollective));
    validateAllocation(graph->collective_list, __FUNCTION__);
    TakyonCollective *collective_desc = &graph->collective_list[graph->collective_count - 1];
    memset(collective_desc, 0, sizeof(TakyonCollective));

    // Name
    validateKeyValue("Collective:", data_ptr, keyword, value, line_count);
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
    if ((collective_desc->type == TAKYON_COLLECTIVE_BARRIER) || (collective_desc->type == TAKYON_COLLECTIVE_REDUCE)) {
      getCollectivePathTree(graph, value, &collective_desc->num_paths, &collective_desc->path_list, &collective_desc->path_tree, line_count);
    } else {
      getCollectivePathList(graph, value, &collective_desc->num_paths, &collective_desc->path_list, line_count);
    }

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
    } else if ((collective_desc->type == TAKYON_COLLECTIVE_BARRIER) || (collective_desc->type == TAKYON_COLLECTIVE_REDUCE)) {
      // NOTES:
      //   Input format guarantees a propert tree structure
      //   Above code guarantees no duplicated paths, which means no cyclic dependencies.
      //   Parsing code guarantees proper tree connections with no un balanced structure
    } else if (collective_desc->type == TAKYON_COLLECTIVE_SCATTER) {
      // Make sure all scatter sources are the same
      TakyonCollectiveConnection *first_coll_path = &collective_desc->path_list[0];
      TakyonConnection *first_path_desc = getPath(graph, first_coll_path->path_id);
      int src_group_id = first_coll_path->src_is_endpointA ? first_path_desc->group_idA : first_path_desc->group_idB;
      for (int i=1; i<collective_desc->num_paths; i++) {
        TakyonCollectiveConnection *coll_path = &collective_desc->path_list[i];
        TakyonConnection *path_desc = getPath(graph, coll_path->path_id);
        int group_id = coll_path->src_is_endpointA ? path_desc->group_idA : path_desc->group_idB;
        if (group_id != src_group_id) {
          fprintf(stderr, "Collective group '%s' is a scatter, but the source group IDs are not all the same.\n", collective_desc->name);
          exit(EXIT_FAILURE);
        }
      }
    } else if (collective_desc->type == TAKYON_COLLECTIVE_GATHER) {
      // Make sure all gather destinations are the same
      TakyonCollectiveConnection *first_coll_path = &collective_desc->path_list[0];
      TakyonConnection *first_path_desc = getPath(graph, first_coll_path->path_id);
      int dest_group_id = first_coll_path->src_is_endpointA ? first_path_desc->group_idB : first_path_desc->group_idA;
      for (int i=1; i<collective_desc->num_paths; i++) {
        TakyonCollectiveConnection *coll_path = &collective_desc->path_list[i];
        TakyonConnection *path_desc = getPath(graph, coll_path->path_id);
        int group_id = coll_path->src_is_endpointA ? path_desc->group_idB : path_desc->group_idA;
        if (group_id != dest_group_id) {
          fprintf(stderr, "Collective group '%s' is a gather, but the destination group IDs are not all the same.\n", collective_desc->name);
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
#define NUM_DEFAULT_PARAMS 17
#define NUM_PATH_PARAMS (NUM_DEFAULT_PARAMS+1)
typedef enum {
  PARAM_TYPE_BOOL,
  PARAM_TYPE_INT,
  PARAM_TYPE_GROUP_INSTANCE,
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

static void setParamDefaultValueGroupInstance(PathParam *param, const char *name, int value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_GROUP_INSTANCE;
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
  param->uint64_listA = NULL;
  param->uint64_listB = NULL;
  param->listA_count = 0;
  param->listB_count = 0;
}

static void setParamDefaultValueAddrList(PathParam *param, const char *name, size_t value) {
  strcpy(param->name, name);
  param->type = PARAM_TYPE_ADDR_LIST;
  param->addr_listA = NULL;
  param->addr_listB = NULL;
  param->listA_count = 0;
  param->listB_count = 0;
}

static void updateParam(TakyonGraph *graph, PathParam *param, char *value, int line_count) {
  static char valueA[MAX_VALUE_BYTES];
  static char valueB[MAX_VALUE_BYTES];

  // Verify only one comma
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
  } else if (param->type == PARAM_TYPE_GROUP_INSTANCE) {
    // Format is <group_name>[index]
    if ((param->intA == -1) && (param->intB == -1)) {
      param->intA = getGroupIdFromInstance(graph, valueA, line_count);
      param->intB = getGroupIdFromInstance(graph, valueB, line_count);
      if ((param->intA == -1) && (param->intB == -1)) {
        fprintf(stderr, "Line %d: The path's group IDs must have at least one valid endpoint (Use '-' to indicate an unused endpoint for a one-sided connection).\n", line_count);
        exit(EXIT_FAILURE);
      }
    } else {
      fprintf(stderr, "Line %d: The path's group IDs have already been set.\n", line_count);
      exit(EXIT_FAILURE);
    }
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
    // Since values are allowed to be specified more than once, onlt the last one take precedence, so free up any previous values
    if (param->uint64_listA != NULL) free(param->uint64_listA);
    if (param->uint64_listB != NULL) free(param->uint64_listB);
    getUInt64List(valueA, &param->listA_count, &param->uint64_listA, line_count);
    getUInt64List(valueB, &param->listB_count, &param->uint64_listB, line_count);
  } else if (param->type == PARAM_TYPE_ADDR_LIST) {
    // Since values are allowed to be specified more than once, onlt the last one take precedence, so free up any previous values
    if (param->addr_listA != NULL) free(param->addr_listA);
    if (param->addr_listB != NULL) free(param->addr_listB);
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
    } else if (dest_params[i].type == PARAM_TYPE_GROUP_INSTANCE) {
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
      dest_params[i].uint64_listA = NULL;
      dest_params[i].uint64_listB = NULL;
      if (dest_params[i].listA_count > 0) {
        dest_params[i].uint64_listA = (uint64_t *)malloc(dest_params[i].listA_count * sizeof(uint64_t));
        if (dest_params[i].uint64_listA == NULL) { fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__); exit(EXIT_FAILURE); }
      }
      if (dest_params[i].listB_count > 0) {
        dest_params[i].uint64_listB = (uint64_t *)malloc(dest_params[i].listB_count * sizeof(uint64_t));
        if (dest_params[i].uint64_listB == NULL) { fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__); exit(EXIT_FAILURE); }
      }
      for (int j=0; j<dest_params[i].listA_count; j++) dest_params[i].uint64_listA[j] = src_params[i].uint64_listA[j];
      for (int j=0; j<dest_params[i].listB_count; j++) dest_params[i].uint64_listB[j] = src_params[i].uint64_listB[j];
    } else if (dest_params[i].type == PARAM_TYPE_ADDR_LIST) {
      dest_params[i].listA_count = src_params[i].listA_count;
      dest_params[i].listB_count = src_params[i].listB_count;
      dest_params[i].addr_listA = NULL;
      dest_params[i].addr_listB = NULL;
      if (dest_params[i].listA_count > 0) {
        dest_params[i].addr_listA = (size_t *)malloc(dest_params[i].listA_count * sizeof(size_t));
        if (dest_params[i].addr_listA == NULL) { fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__); exit(EXIT_FAILURE); }
      }
      if (dest_params[i].listB_count > 0) {
        dest_params[i].addr_listB = (size_t *)malloc(dest_params[i].listB_count * sizeof(size_t));
        if (dest_params[i].addr_listB == NULL) { fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__); exit(EXIT_FAILURE); }
      }
      for (int j=0; j<dest_params[i].listA_count; j++) dest_params[i].addr_listA[j] = src_params[i].addr_listA[j];
      for (int j=0; j<dest_params[i].listB_count; j++) dest_params[i].addr_listB[j] = src_params[i].addr_listB[j];
    }
  }
}

static uint64_t *copyUInt64List(uint64_t *list, int count) {
  if (count == 0) return NULL;

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
  if (count == 0) return NULL;

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
      if (param->uint64_listA != NULL) free(param->uint64_listA);
      if (param->uint64_listB != NULL) free(param->uint64_listB);
    } else if ((strcmp(param->name, "SenderAddrList:") == 0) || (strcmp(param->name, "RecverAddrList:") == 0)) {
      if (param->addr_listA != NULL) free(param->addr_listA);
      if (param->addr_listB != NULL) free(param->addr_listB);
    }
  }
}

static void copyParamsToPathAttributes(TakyonPathAttributes *attrs, bool is_endpointA, PathParam *path_params, int num_params, const char *interconnectA, const char *interconnectB) {
  attrs->is_endpointA = is_endpointA;
  strncpy(attrs->interconnect, is_endpointA ? interconnectA : interconnectB, TAKYON_MAX_INTERCONNECT_CHARS);
  for (int i=0; i<num_params; i++) {
    PathParam *param = &path_params[i];
    if (strcmp(param->name, "IsPolling:") == 0) {
      attrs->is_polling = is_endpointA ? param->boolA : param->boolB;
    } else if (strcmp(param->name, "AbortOnFailure:") == 0) {
      attrs->abort_on_failure = is_endpointA ? param->boolA : param->boolB;
    } else if (strcmp(param->name, "Verbosity:") == 0) {
      attrs->verbosity = is_endpointA ? param->uint64A : param->uint64B;
    } else if (strcmp(param->name, "PathCreateTimeout:") == 0) {
      attrs->path_create_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "SendStartTimeout:") == 0) {
      attrs->send_start_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "SendFinishTimeout:") == 0) {
      attrs->send_finish_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "RecvStartTimeout:") == 0) {
      attrs->recv_start_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "RecvFinishTimeout:") == 0) {
      attrs->recv_finish_timeout = is_endpointA ? param->doubleA : param->doubleB;
    } else if (strcmp(param->name, "PathDestroyTimeout:") == 0) {
      attrs->path_destroy_timeout = is_endpointA ? param->doubleA : param->doubleB;
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
  setParamDefaultValueDouble(&default_params[3], "PathCreateTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[4], "SendStartTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[5], "SendFinishTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[6], "RecvStartTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[7], "RecvFinishTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueDouble(&default_params[8], "PathDestroyTimeout:", TAKYON_WAIT_FOREVER);
  setParamDefaultValueCompletionMethod(&default_params[9], "SendCompletionMethod:", TAKYON_BLOCKING);
  setParamDefaultValueCompletionMethod(&default_params[10], "RecvCompletionMethod:", TAKYON_BLOCKING);
  setParamDefaultValueInt(&default_params[11], "NBufsAtoB:", 1);
  setParamDefaultValueInt(&default_params[12], "NBufsBtoA:", 1);
  setParamDefaultValueUInt64List(&default_params[13], "SenderMaxBytesList:", 0);
  setParamDefaultValueUInt64List(&default_params[14], "RecverMaxBytesList:", 0);
  setParamDefaultValueAddrList(&default_params[15], "SenderAddrList:", 0);
  setParamDefaultValueAddrList(&default_params[16], "RecverAddrList:", 0);

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

      // Set path attribute defaults (make sure endpoints are unset)
      PathParam path_params[NUM_PATH_PARAMS];
      memset(path_params, 0, NUM_PATH_PARAMS*sizeof(PathParam));
      copyParams(path_params, default_params, NUM_DEFAULT_PARAMS);
      setParamDefaultValueGroupInstance(&path_params[NUM_DEFAULT_PARAMS], "Endpoints:", -1);

      // Endpoints (each endpoint is a group ID)
      // NOTE: if one-sided, then one of the group IDs can be "-" (i.e. remain -1)
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      validateKeyValue("Endpoints:", data_ptr, keyword, value, line_count);
      int param_index = getParamIndex(keyword, path_params, NUM_PATH_PARAMS);
      updateParam(graph, &path_params[param_index], value, line_count);

      // InterconnectA
      static char interconnectA[TAKYON_MAX_INTERCONNECT_CHARS];
      interconnectA[0] = '\0';
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      validateKeyValue("InterconnectA:", data_ptr, keyword, value, line_count);
      int value_length = (int)strlen(value);
      snprintf(interconnectA, TAKYON_MAX_INTERCONNECT_CHARS, "%.*s", value_length, value);

      // InterconnectB
      static char interconnectB[TAKYON_MAX_INTERCONNECT_CHARS];
      interconnectB[0] = '\0';
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      validateKeyValue("InterconnectB:", data_ptr, keyword, value, line_count);
      value_length = (int)strlen(value);
      snprintf(interconnectB, TAKYON_MAX_INTERCONNECT_CHARS, "%.*s", value_length, value);

      // Get path attributes
      data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
      param_index = getParamIndex(keyword, path_params, NUM_PATH_PARAMS);
      while (param_index >= 0) {
        // Update path params
        updateParam(graph, &path_params[param_index], value, line_count);
        // Get next line
        data_ptr = getNextKeyValue(data_ptr, keyword, value, &line_count);
        param_index = getParamIndex(keyword, path_params, NUM_PATH_PARAMS);
      }

      // Do some error checking before setting the Takyon path attributes
      if ((path_params[NUM_DEFAULT_PARAMS].intA < 0) && (path_params[NUM_DEFAULT_PARAMS].intB < 0)) {
        fprintf(stderr, "Line %d: 'Endpoints:' for A and B was not defined for this path\n", line_count-1);
        exit(EXIT_FAILURE);
      }
      if (path_params[NUM_DEFAULT_PARAMS].intA == path_params[NUM_DEFAULT_PARAMS].intB) {
        fprintf(stderr, "Line %d: 'Endpoints:' for A and B can not be the same\n", line_count-1);
        exit(EXIT_FAILURE);
      }
      if (path_params[NUM_DEFAULT_PARAMS].intA < 0) {
        if (strcmp(interconnectA, "-") != 0) {
          fprintf(stderr, "Line %d: 'InterconnectA' should be defined as '-' since this is a one-sided interconnect, and endpoint B is a valid endpoint\n", line_count-1);
          exit(EXIT_FAILURE);
        }
      } else {
        if (strlen(interconnectA) == 0) {
          fprintf(stderr, "Line %d: 'InterconnectA' was not defined for this path\n", line_count-1);
          exit(EXIT_FAILURE);
        }
      }
      if (path_params[NUM_DEFAULT_PARAMS].intB < 0) {
        if (strcmp(interconnectB, "-") != 0) {
          fprintf(stderr, "Line %d: 'InterconnectB' should be defined as '-' since this is a one-sided interconnect, and endpoint A is a valid endpoint\n", line_count-1);
          exit(EXIT_FAILURE);
        }
      } else {
        if (strlen(interconnectB) == 0) {
          fprintf(stderr, "Line %d: 'InterconnectB' was not defined for this path\n", line_count-1);
          exit(EXIT_FAILURE);
        }
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
      path_desc->group_idA = path_params[NUM_DEFAULT_PARAMS].intA;
      path_desc->group_idB = path_params[NUM_DEFAULT_PARAMS].intB;
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

  // Groups
  data_ptr = parseGroups(graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("GroupIDs:\n");
  for (int i=0; i<graph->group_count; i++) {
    TakyonGroup *group_desc = &graph->group_list[i];
    printf("  %s: instances=%d\n", group_desc->name, group_desc->instances);
  }
#endif

  // Processes
  data_ptr = parseProcesses(graph, data_ptr, keyword, value, &line_count);
  // Buffers (get mapped to a process)
  data_ptr = parseBuffers(process_id, graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Processes:\n");
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    printf("  Process %d: GroupIDs:", process_desc->id);
    for (int j=0; j<process_desc->thread_count; j++) {
      TakyonGroup *group_desc = takyonGetGroup(graph, process_desc->thread_list[j].group_id);
      int instance = process_desc->thread_list[j].group_id - group_desc->starting_group_id;
      printf(" %s[%d]", group_desc->name, instance);
    }
    printf("\n");
    for (int j=0; j<process_desc->buffer_count; j++) {
      TakyonBuffer *buffer = &process_desc->buffer_list[j];
      printf("    Buffer '%s': where='%s', bytes=%ju, addr=%p\n", buffer->name, buffer->where, buffer->bytes, buffer->addr);
    }
  }
#endif

  // Paths
  data_ptr = parsePaths(graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Paths = %d\n", graph->path_count);
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    TakyonGroup *group_descA = NULL;
    TakyonGroup *group_descB = NULL;
    int instanceA = -1;
    int instanceB = -1;
    if (path_desc->group_idA >= 0) {
      group_descA = takyonGetGroup(graph, path_desc->group_idA);
      instanceA = path_desc->group_idA - group_descA->starting_group_id;
    }
    if (path_desc->group_idB >= 0) {
      group_descB = takyonGetGroup(graph, path_desc->group_idB);
      instanceB = path_desc->group_idB - group_descB->starting_group_id;
    }
    if (instanceA >= 0 && instanceB >= 0) {
      printf("  Path=%d: EndpointA = %s[%d] (global ID is %d), EndpointB = %s[%d] (global ID is %d)\n", path_desc->id,
             group_descA->name, instanceA, path_desc->group_idA,
             group_descB->name, instanceB, path_desc->group_idB);
    } else if (instanceA >= 0) {
      printf("  Path=%d: EndpointA = %s[%d] (global ID is %d), EndpointB is unused\n", path_desc->id, group_descA->name, instanceA, path_desc->group_idA);
    } else {
      printf("  Path=%d: EndpointA is unused, EndpointB = %s[%d] (global ID is %d)\n", path_desc->id, group_descB->name, instanceB, path_desc->group_idB);
    }

    // Endpoint A
    if (instanceA >= 0) {
      printf("    Endpoint A Attributes:\n");
      printf("      Interconnect: %s\n", path_desc->attrsA.interconnect);
      printf("      is_polling: %s\n", path_desc->attrsA.is_polling ? "Yes" : "No");
      printf("      abort_on_failure: %s\n", path_desc->attrsA.abort_on_failure ? "Yes" : "No");
      printf("      verbosity: 0x%jx\n", path_desc->attrsA.verbosity);
      printf("      path_create_timeout: %lf\n", path_desc->attrsA.path_create_timeout);
      printf("      send_start_timeout: %lf\n", path_desc->attrsA.send_start_timeout);
      printf("      send_finish_timeout: %lf\n", path_desc->attrsA.send_finish_timeout);
      printf("      recv_start_timeout: %lf\n", path_desc->attrsA.recv_start_timeout);
      printf("      recv_finish_timeout: %lf\n", path_desc->attrsA.recv_finish_timeout);
      printf("      path_destroy_timeout: %lf\n", path_desc->attrsA.path_destroy_timeout);
      printf("      send_completion_method: %s\n", (path_desc->attrsA.send_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_IS_SEND_FINISHED");
      printf("      recv_completion_method: %s\n", (path_desc->attrsA.recv_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_IS_SEND_FINISHED");
      printf("      nbufs_AtoB: %d\n", path_desc->attrsA.nbufs_AtoB);
      printf("      nbufs_BtoA: %d\n", path_desc->attrsA.nbufs_BtoA);
      printf("      sender_max_bytes_list:");
      for (int j=0; j<path_desc->attrsA.nbufs_AtoB; j++) {
        printf(" %ju", path_desc->attrsA.sender_max_bytes_list[j]);
      }
      printf("\n");
      printf("      recver_max_bytes_list:");
      for (int j=0; j<path_desc->attrsA.nbufs_BtoA; j++) {
        printf(" %ju", path_desc->attrsA.recver_max_bytes_list[j]);
      }
      printf("\n");
      printf("      sender_addr_list:");
      for (int j=0; j<path_desc->attrsA.nbufs_AtoB; j++) {
        printf(" %p", (void *)path_desc->attrsA.sender_addr_list[j]);
      }
      printf("\n");
      printf("      recver_addr_list:");
      for (int j=0; j<path_desc->attrsA.nbufs_BtoA; j++) {
        printf(" %p", (void *)path_desc->attrsA.recver_addr_list[j]);
      }
      printf("\n");
    }

    // Endpoint B
    if (instanceB >= 0) {
      printf("    Endpoint B Attributes:\n");
      printf("      Interconnect: %s\n", path_desc->attrsB.interconnect);
      printf("      is_polling: %s\n", path_desc->attrsB.is_polling ? "Yes" : "No");
      printf("      abort_on_failure: %s\n", path_desc->attrsB.abort_on_failure ? "Yes" : "No");
      printf("      verbosity: 0x%jx\n", path_desc->attrsB.verbosity);
      printf("      path_create_timeout: %lf\n", path_desc->attrsB.path_create_timeout);
      printf("      send_start_timeout: %lf\n", path_desc->attrsB.send_start_timeout);
      printf("      send_finish_timeout: %lf\n", path_desc->attrsB.send_finish_timeout);
      printf("      recv_start_timeout: %lf\n", path_desc->attrsB.recv_start_timeout);
      printf("      recv_finish_timeout: %lf\n", path_desc->attrsB.recv_finish_timeout);
      printf("      path_destroy_timeout: %lf\n", path_desc->attrsB.path_destroy_timeout);
      printf("      send_completion_method: %s\n", (path_desc->attrsB.send_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_IS_SEND_FINISHED");
      printf("      recv_completion_method: %s\n", (path_desc->attrsB.recv_completion_method == TAKYON_BLOCKING) ? "TAKYON_BLOCKING" : "TAKYON_USE_IS_SEND_FINISHED");
      printf("      nbufs_AtoB: %d\n", path_desc->attrsB.nbufs_AtoB);
      printf("      nbufs_BtoA: %d\n", path_desc->attrsB.nbufs_BtoA);
      printf("      sender_max_bytes_list:");
      for (int j=0; j<path_desc->attrsB.nbufs_AtoB; j++) {
        printf(" %ju", path_desc->attrsB.sender_max_bytes_list[j]);
      }
      printf("\n");
      printf("      recver_max_bytes_list:");
      for (int j=0; j<path_desc->attrsB.nbufs_BtoA; j++) {
        printf(" %ju", path_desc->attrsB.recver_max_bytes_list[j]);
      }
      printf("\n");
      printf("      sender_addr_list:");
      for (int j=0; j<path_desc->attrsB.nbufs_AtoB; j++) {
        printf(" %p", (void *)path_desc->attrsB.sender_addr_list[j]);
      }
      printf("\n");
      printf("      recver_addr_list:");
      for (int j=0; j<path_desc->attrsB.nbufs_BtoA; j++) {
        printf(" %p", (void *)path_desc->attrsB.recver_addr_list[j]);
      }
      printf("\n");
    }
  }
#endif

  // Collective groups
  data_ptr = parseCollectives(graph, data_ptr, keyword, value, &line_count);
#ifdef DEBUG_MESSAGE
  printf("Collectives = %d\n", graph->collective_count);
  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollective *collective_desc = &graph->collective_list[i];
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

static void freePathTree(TakyonPathTree *path_tree) {
  for (int i=0; i<path_tree->num_children; i++) {
    TakyonPathTree *child = &path_tree->children[i];
    freePathTree(child);
  }
  if (path_tree->children != NULL) free(path_tree->children);
  if (path_tree->parent == NULL) free(path_tree);
}

void takyonFreeGraphDescription(TakyonGraph *graph, int my_process_id) {
  for (int i=0; i<graph->group_count; i++) {
    free(graph->group_list[i].name);
  }
  free(graph->group_list);

  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process = &graph->process_list[i];
    free(process->thread_list);
    for (int j=0; j<process->buffer_count; j++) {
      TakyonBuffer *buffer = &process->buffer_list[j];
      // NOTE: This function is application defined
      if (process->id == my_process_id) {
        // This is the process that allocated the memory
        appFreeMemory(buffer->where, buffer->user_data, buffer->addr);
      }
      free(buffer->name);
      free(buffer->where);
    }
    if (process->buffer_count > 0) free(process->buffer_list);
  }
  free(graph->process_list);

  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    takyonFreeAttributes(path_desc->attrsA);
    takyonFreeAttributes(path_desc->attrsB);
  }
  free(graph->path_list);

  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollective *collective = &graph->collective_list[i];
    free(collective->name);
    free(collective->path_list);
    if (collective->path_tree != NULL) freePathTree(collective->path_tree);
  }
  if (graph->collective_count > 0) free(graph->collective_list);

  free(graph);
}

void takyonCreateGroupPaths(TakyonGraph *graph, int group_id) {
  // Create all the paths
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    if (path_desc->group_idA == group_id) {
      TakyonPathAttributes *attrs = &path_desc->attrsA;
      path_desc->pathA = takyonCreate(attrs);
      if (path_desc->pathA == NULL) {
        fprintf(stderr, "%s(): Failed to create path: path=%s\n", __FUNCTION__, attrs->interconnect);
        exit(EXIT_FAILURE);
      }
    } else if (path_desc->group_idB == group_id) {
      TakyonPathAttributes *attrs = &path_desc->attrsB;
      path_desc->pathB = takyonCreate(attrs);
      if (path_desc->pathB == NULL) {
        fprintf(stderr, "%s(): Failed to create path: path=%s\n", __FUNCTION__, attrs->interconnect);
        exit(EXIT_FAILURE);
      }
    }
  }
}

void takyonDestroyGroupPaths(TakyonGraph *graph, int group_id) {
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    if (path_desc->group_idA == group_id) {
      char *error_message = takyonDestroy(&path_desc->pathA);
      if (error_message != NULL) {
        fprintf(stderr, "%s(): Failed to destroy endpoint A of path: %s\n", __FUNCTION__, error_message);
        free(error_message);
        exit(EXIT_FAILURE);
      }
    } else if (path_desc->group_idB == group_id) {
      char *error_message = takyonDestroy(&path_desc->pathB);
      if (error_message != NULL) {
        fprintf(stderr, "%s(): Failed to destroy endpoint B of path: %s\n", __FUNCTION__, error_message);
        free(error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

TakyonCollectiveBarrier *takyonGetBarrier(TakyonGraph *graph, const char *name, int group_id) {
  TakyonCollectiveBarrier *collective = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_BARRIER)) {
    // Get the parent
    TakyonPath *parent_path = NULL;
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->group_idB == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idA == group_id))) {
        if (parent_path != NULL) {
          TakyonGroup *group = takyonGetGroup(graph, group_id);
          int instance = group_id - group->starting_group_id;
          fprintf(stderr, "%s(): The collective barrier '%s' has two tree nodes wth the same group ID %s[%d]\n", __FUNCTION__, name, group->name, instance);
          exit(EXIT_FAILURE);
        }
        parent_path = (collective_path->src_is_endpointA) ? path_desc->pathB: path_desc->pathA;
      }
    }
    // Get the number of children
    int nchildren = 0;
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
        nchildren++;
      }
    }
    // Create the child list
    TakyonPath **child_path_list = NULL;
    int child_index = 0;
    if (nchildren > 0) {
      child_path_list = (TakyonPath **)calloc(nchildren, sizeof(TakyonPath *));
      validateAllocation(child_path_list, __FUNCTION__);
      for (int j=0; j<collective_desc->num_paths; j++) {
        TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
        TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
        if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
          child_path_list[child_index] = (collective_path->src_is_endpointA) ? path_desc->pathA: path_desc->pathB;
          child_index++;
        }
      }
    }
    // Define collective
    collective = takyonBarrierInit(nchildren, parent_path, child_path_list);
    free(child_path_list);
  }
  if (collective == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return collective;
}

TakyonCollectiveReduce *takyonGetReduce(TakyonGraph *graph, const char *name, int group_id) {
  TakyonCollectiveReduce *collective = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_REDUCE)) {
    // Get the parent
    TakyonPath *parent_path = NULL;
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->group_idB == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idA == group_id))) {
        if (parent_path != NULL) {
          TakyonGroup *group = takyonGetGroup(graph, group_id);
          int instance = group_id - group->starting_group_id;
          fprintf(stderr, "%s(): The collective reduce '%s' has two tree nodes wth the same group ID %s[%d]\n", __FUNCTION__, name, group->name, instance);
          exit(EXIT_FAILURE);
        }
        parent_path = (collective_path->src_is_endpointA) ? path_desc->pathB: path_desc->pathA;
      }
    }
    // Get the number of children
    int nchildren = 0;
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
        nchildren++;
      }
    }
    // Create the child list
    TakyonPath **child_path_list = NULL;
    int child_index = 0;
    if (nchildren > 0) {
      child_path_list = (TakyonPath **)calloc(nchildren, sizeof(TakyonPath *));
      validateAllocation(child_path_list, __FUNCTION__);
      for (int j=0; j<collective_desc->num_paths; j++) {
        TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
        TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
        if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
          child_path_list[child_index] = (collective_path->src_is_endpointA) ? path_desc->pathA: path_desc->pathB;
          child_index++;
        }
      }
    }
    // Define collective
    collective = takyonReduceInit(nchildren, parent_path, child_path_list);
    free(child_path_list);
  }
  if (collective == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return collective;
}

TakyonCollectiveOne2One *takyonGetOne2One(TakyonGraph *graph, const char *name, int group_id) {
  TakyonCollectiveOne2One *collective = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_ONE2ONE)) {
    // Count the source and dest paths in the group instance
    int num_src_paths = 0;
    int num_dest_paths = 0;
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
        num_src_paths++;
      }
      if ((collective_path->src_is_endpointA && (path_desc->group_idB == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idA == group_id))) {
        num_dest_paths++;
      }
    }
    // Allocate the memory to temporarily store the source and dest paths
    TakyonPath **src_path_list = NULL;
    TakyonPath **dest_path_list = NULL;
    if (num_src_paths > 0) {
      src_path_list = (TakyonPath **)calloc(num_src_paths, sizeof(TakyonPath *));
      validateAllocation(src_path_list, __FUNCTION__);
    }
    if (num_dest_paths > 0) {
      dest_path_list = (TakyonPath **)calloc(num_dest_paths, sizeof(TakyonPath *));
      validateAllocation(dest_path_list, __FUNCTION__);
    }
    // Re-count and store the source and dest paths in the temporary arrays
    num_src_paths = 0;
    num_dest_paths = 0;
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path = &collective_desc->path_list[j];
      TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
      if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
        src_path_list[num_src_paths] = (collective_path->src_is_endpointA) ? path_desc->pathA: path_desc->pathB;
        num_src_paths++;
      }
      if ((collective_path->src_is_endpointA && (path_desc->group_idB == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idA == group_id))) {
        dest_path_list[num_dest_paths] = (collective_path->src_is_endpointA) ? path_desc->pathB: path_desc->pathA;
        num_dest_paths++;
      }
    }
    // Store the source and dest paths, for this group instance, in a collective data structure (a separate allocation will be done so the temp lists can be freed)
    collective = takyonOne2OneInit(collective_desc->num_paths, num_src_paths, num_dest_paths, src_path_list, dest_path_list);
    // Free the temp lists
    if (src_path_list != NULL) free(src_path_list);
    if (dest_path_list != NULL) free(dest_path_list);
  }
  if (collective == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return collective;
}

TakyonScatterSrc *takyonGetScatterSrc(TakyonGraph *graph, const char *name, int group_id) {
  TakyonScatterSrc *scatter_src = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_SCATTER)) {
    TakyonCollectiveConnection *collective_path = &collective_desc->path_list[0];
    TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
    if ((collective_path->src_is_endpointA && (path_desc->group_idA == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idB == group_id))) {
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
  if (scatter_src == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return scatter_src;
}

TakyonScatterDest *takyonGetScatterDest(TakyonGraph *graph, const char *name, int group_id) {
  TakyonScatterDest *scatter_dest = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_SCATTER)) {
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path2 = &collective_desc->path_list[j];
      TakyonConnection *path_desc2 = getPath(graph, collective_path2->path_id);
      if ((collective_path2->src_is_endpointA && (path_desc2->group_idB == group_id)) || ((!collective_path2->src_is_endpointA) && (path_desc2->group_idA == group_id))) {
        int group_id2 = (collective_path2->src_is_endpointA) ? path_desc2->group_idB: path_desc2->group_idA;
        if (group_id == group_id2) {
          TakyonPath *path = (collective_path2->src_is_endpointA) ? path_desc2->pathB: path_desc2->pathA;
          scatter_dest = takyonScatterDestInit(collective_desc->num_paths, j, path);
          break;
        }
      }
    }
  }
  if (scatter_dest == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return scatter_dest;
}

TakyonGatherSrc *takyonGetGatherSrc(TakyonGraph *graph, const char *name, int group_id) {
  TakyonGatherSrc *gather_src = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_GATHER)) {
    for (int j=0; j<collective_desc->num_paths; j++) {
      TakyonCollectiveConnection *collective_path2 = &collective_desc->path_list[j];
      TakyonConnection *path_desc2 = getPath(graph, collective_path2->path_id);
      if ((collective_path2->src_is_endpointA && (path_desc2->group_idA == group_id)) || ((!collective_path2->src_is_endpointA) && (path_desc2->group_idB == group_id))) {
        int group_id2 = (collective_path2->src_is_endpointA) ? path_desc2->group_idA: path_desc2->group_idB;
        if (group_id == group_id2) {
          TakyonPath *path = (collective_path2->src_is_endpointA) ? path_desc2->pathA: path_desc2->pathB;
          gather_src = takyonGatherSrcInit(collective_desc->num_paths, j, path);
          break;
        }
      }
    }
  }
  if (gather_src == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return gather_src;
}

TakyonGatherDest *takyonGetGatherDest(TakyonGraph *graph, const char *name, int group_id) {
  TakyonGatherDest *gather_dest = NULL;
  TakyonCollective *collective_desc = takyonGetCollective(graph, name);
  if ((collective_desc != NULL) && (collective_desc->type == TAKYON_COLLECTIVE_GATHER)) {
    TakyonCollectiveConnection *collective_path = &collective_desc->path_list[0];
    TakyonConnection *path_desc = getPath(graph, collective_path->path_id);
    if ((collective_path->src_is_endpointA && (path_desc->group_idB == group_id)) || ((!collective_path->src_is_endpointA) && (path_desc->group_idA == group_id))) {
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
  if (gather_dest == NULL) {
    fprintf(stderr, "%s(): Failed to find the collective group '%s'\n", __FUNCTION__, name);
    exit(EXIT_FAILURE);
  }
  return gather_dest;
}

static void printPathTree(TakyonPathTree *tree_node, TakyonCollective *collective_desc) {
  if (tree_node->parent != NULL) {
    TakyonCollectiveConnection *path = &collective_desc->path_list[tree_node->path_index];
    printf(" %d:%s", path->path_id, path->src_is_endpointA ? "A->B" : "B->A");
  }
  if ((tree_node->parent != NULL) && (tree_node->num_children > 0)) printf(" (");
  for (int i=0; i<tree_node->num_children; i++) {
    printPathTree(&tree_node->children[i], collective_desc);
  }
  if ((tree_node->parent != NULL) && (tree_node->num_children > 0)) printf(" )");
}

void takyonPrintGraph(TakyonGraph *graph) {
  printf("  Groups:\n");
  for (int i=0; i<graph->group_count; i++) {
    TakyonGroup *group_desc = &graph->group_list[i];
    printf("    %s: instances=%d\n", group_desc->name, group_desc->instances);
  }

  printf("  Processes:\n");
  for (int i=0; i<graph->process_count; i++) {
    TakyonProcess *process_desc = &graph->process_list[i];
    printf("    Process %d: GroupIDs:", process_desc->id);
    for (int j=0; j<process_desc->thread_count; j++) {
      TakyonGroup *group_desc = takyonGetGroup(graph, process_desc->thread_list[j].group_id);
      int instance = process_desc->thread_list[j].group_id - group_desc->starting_group_id;
      printf(" %s[%d]", group_desc->name, instance);
    }
    printf("\n");
    for (int j=0; j<process_desc->buffer_count; j++) {
      TakyonBuffer *buffer = &process_desc->buffer_list[j];
      printf("      Buffer '%s': where='%s', bytes=%ju, addr=%p\n", buffer->name, buffer->where, buffer->bytes, buffer->addr);
    }
  }

  printf("  Paths:\n");
  for (int i=0; i<graph->path_count; i++) {
    TakyonConnection *path_desc = &graph->path_list[i];
    TakyonGroup *group_descA = NULL;
    TakyonGroup *group_descB = NULL;
    int instanceA = -1;
    int instanceB = -1;
    if (path_desc->group_idA >= 0) {
      group_descA = takyonGetGroup(graph, path_desc->group_idA);
      instanceA = path_desc->group_idA - group_descA->starting_group_id;
    }
    if (path_desc->group_idB >= 0) {
      group_descB = takyonGetGroup(graph, path_desc->group_idB);
      instanceB = path_desc->group_idB - group_descB->starting_group_id;
    }
    if (instanceA >= 0 && instanceB >= 0) {
      printf("    Path %d: %s[%d] <--> %s[%d]\n", path_desc->id, group_descA->name, instanceA, group_descB->name, instanceB);
      printf("      Interconnect A: %s\n", path_desc->attrsA.interconnect);
      printf("      Interconnect B: %s\n", path_desc->attrsB.interconnect);
    } else if (instanceA >= 0) {
      printf("    Path %d: Endpoint A = %s[%d], Endpoint B unused\n", path_desc->id, group_descA->name, instanceA);
      printf("      Interconnect A: %s\n", path_desc->attrsA.interconnect);
      printf("      Interconnect B: -\n");
    } else {
      printf("    Path %d: Endpoint A unused, Endpoint B = %s[%d]\n", path_desc->id, group_descB->name, instanceB);
      printf("      Interconnect A: -\n");
      printf("      Interconnect B: %s\n", path_desc->attrsB.interconnect);
    }
  }

  printf("  Collectives:\n");
  if (graph->collective_count == 0) {
    printf("    -\n");
  }
  for (int i=0; i<graph->collective_count; i++) {
    TakyonCollective *collective_desc = &graph->collective_list[i];
    printf("    '%s', Type=%s, Paths:", collective_desc->name, collectiveTypeToName(collective_desc->type));
    if (collective_desc->path_tree != NULL) {
      printPathTree(collective_desc->path_tree, collective_desc);
    } else {
      for (int j=0; j<collective_desc->num_paths; j++) {
        TakyonCollectiveConnection *path = &collective_desc->path_list[j];
        printf(" %d:%s", path->path_id, path->src_is_endpointA ? "A->B" : "B->A");
      }
    }
    printf("\n");
  }
}
