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

// -----------------------------------------------------------------------------
// Description:
//   Use these function to parse Takyon's attr->interconnect[] string
//   This is generaly needed in the first part of the create() call made by any
//   of Takyon's supported interconnect interfaces
// -----------------------------------------------------------------------------

#include "takyon_private.h"

// This is a simple parse with a few requirements
//  - Args are separate by a single space
//  - First arg does not start with a minus
//  - Flags are always the form " -<named_flag>"
//  - named args with values are always the format: " -<arg_name> <value>"
//  - Full example: "socket -IP 192.168.0.21 -port 23454"

static int argCount(const char *arguments) {
  int count = 1;
  const char *space_ptr = strchr(arguments, ' ');
  while (space_ptr != NULL) {
    count++;
    space_ptr++;
    space_ptr = strchr(space_ptr, ' ');
  }
  return count;
}

static const char *argPointer(const char *arguments, const int index) {
  const int arg_count = argCount(arguments);
  if (index >= arg_count) return NULL;
  const char *arg_ptr = arguments;
  for (int i=0; i<index; i++) {
    arg_ptr = strchr(arg_ptr, ' ');
    arg_ptr++;
  }
  return arg_ptr;
}

static const char *endOfArg(const char *arg) {
  const char *end = arg;
  while ((*end != ' ') && (*end != '\0')) {
    end++;
  }
  if (arg == end) return NULL;
  return end;
}

static int argLength(const char *arg_start) {
  const char *arg_end = endOfArg(arg_start);
  if (arg_end == NULL) return 0;
  int num_chars = (int)(arg_end - arg_start);
  return num_chars;
}

static const char *argByName(const char *arguments, const char *name) {
  const int name_length = (int)strlen(name);
  const int arg_count = argCount(arguments);
  for (int i=1; i<arg_count; i++) {
    const char *arg_start = argPointer(arguments, i);
    int num_chars = 0;
    if (arg_start != NULL) {
      num_chars = argLength(arg_start);
    }
    if (num_chars == name_length) {
      // Found the correct size arg, so see if the characters are all the same
      bool matched = true;
      for (int j=0; j<name_length; j++) {
        if (arg_start[j] != name[j]) {
          matched = false;
          break;
        }
      }
      if (matched) {
        return arg_start;
      }
    }
  }
  return NULL;
}

bool argGet(const char *arguments, const int index, char *result, const int max_chars, char *error_message) {
  const char *arg_start = argPointer(arguments, index);
  if (arg_start == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Index=%d out of range, not enough arguments.\n", index);
    return false;
  }
  int num_chars = argLength(arg_start);
  if (num_chars >= max_chars) {
    TAKYON_RECORD_ERROR(error_message, "Not enough chars to hold text. Max chars=%d, needed chars=%d, args='%s'\n", max_chars, num_chars, arguments);
    return false;
  }

  for (int i=0; i<num_chars; i++) {
    result[i] = arg_start[i];
  }
  result[num_chars] = '\0';

  return true;
}

bool argGetFlag(const char *arguments, const char *name) {
  const char *arg_start = argByName(arguments, name);
  if (arg_start != NULL) {
    return true;
  }
  return false;
}

bool argGetText(const char *arguments, const char *name, char *result, const int max_chars, bool *found_ret, char *error_message) {
  *found_ret = false;
  const char *arg_start = argByName(arguments, name);
  if (arg_start == NULL) {
    return true;
  }
  // Find the value text
  const char *value_ptr = strchr(arg_start, ' ');
  if (value_ptr == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Arg '%s' missing its value in argument list '%s'\n", name, arguments);
    return false;
  }
  value_ptr++;
  // Get the length of the value text
  int num_chars = argLength(value_ptr);
  if (num_chars == 0) {
    TAKYON_RECORD_ERROR(error_message, "Arg '%s' missing its value in argument list '%s'\n", name, arguments);
    return false;
  }
  if (num_chars >= max_chars) {
    TAKYON_RECORD_ERROR(error_message, "Not enough chars to hold text. Max chars=%d, needed chars=%d, args='%s'\n", max_chars, num_chars, arguments);
    return false;
  }
  // Copy the value text to the result
  for (int i=0; i<num_chars; i++) {
    result[i] = value_ptr[i];
  }
  result[num_chars] = '\0';

  *found_ret = true;
  return true;
}

bool argGetInt(const char *arguments, const char *name, int *result, bool *found_ret, char *error_message) {
  char value_text[MAX_TAKYON_INTERCONNECT_CHARS];
  if (!argGetText(arguments, name, value_text, MAX_TAKYON_INTERCONNECT_CHARS, found_ret, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Failed to get int value for arg '%s'.\n", name);
    return false;
  }
  if (!(*found_ret)) return true; // Argument not found
  int tokens = sscanf(value_text, "%d", result);
  if (tokens != 1) {
    TAKYON_RECORD_ERROR(error_message, "Value for arg '%s' is not an int: '%s'.\n", name, value_text);
    return false;
  }
  return true;
}

bool argGetFloat(const char *arguments, const char *name, float *result, bool *found_ret, char *error_message) {
  char value_text[MAX_TAKYON_INTERCONNECT_CHARS];
  if (!argGetText(arguments, name, value_text, MAX_TAKYON_INTERCONNECT_CHARS, found_ret, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Failed to get float value for arg '%s'.\n", name);
    return false;
  }
  if (!(*found_ret)) return true; // Argument not found
  int tokens = sscanf(value_text, "%f", result);
  if (tokens != 1) {
    TAKYON_RECORD_ERROR(error_message, "Value for arg '%s' is not a float: '%s'.\n", name, value_text);
    return false;
  }
  return true;
}
