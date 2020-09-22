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

// -----------------------------------------------------------------------------
// Description:
//   This is a template for creating a new Takyon interface.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

GLOBAL_VISIBILITY bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknSend() is not yet implemented.\n");
  return false;
}

GLOBAL_VISIBILITY bool tknIsSendFinished(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknIsSendFinished() is not yet implemented.\n");
  return false;
}

GLOBAL_VISIBILITY bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknRecv() is not yet implemented.\n");
  return false;
}

GLOBAL_VISIBILITY bool tknDestroy(TakyonPath *path) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknDestroy() is not yet implemented.\n");
  return false;
}

GLOBAL_VISIBILITY bool tknCreate(TakyonPath *path) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknCreate() is not yet implemented.\n");
  return false;
}

#ifdef BUILD_STATIC_LIB
// Requirements only for static libraries:
//  - Prototyped in takyon_private.h
//  - Make a call to this function in the file utils_shared_libraries.c in the function sharedLibraryGetInterconnectFunctionPointers()
void setTemplateFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknIsSendFinished = tknIsSendFinished;
  private_path->tknRecv = tknRecv;
  private_path->tknDestroy = tknDestroy;
}
#endif
