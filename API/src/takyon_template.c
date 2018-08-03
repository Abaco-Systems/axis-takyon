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
//   This is a template for creating a new Takyon interface.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#ifdef _WIN32
static
#endif
bool tknSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknSendStrided() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknSend() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknSendTest() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknRecvStrided() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknRecv() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknDestroy(TakyonPath *path) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknDestroy() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
static
#endif
bool tknCreate(TakyonPath *path) {
  TAKYON_RECORD_ERROR(path->attrs.error_message, "tknCreate() is not yet implemented.\n");
  return false;
}

#ifdef _WIN32
// This needs to be prototyped in takyon_private.h only for _WIN32
void setTemplateFunctionPointers(TakyonPrivatePath *private_path) {
  private_path->tknCreate = tknCreate;
  private_path->tknSend = tknSend;
  private_path->tknSendStrided = tknSendStrided;
  private_path->tknSendTest = tknSendTest;
  private_path->tknRecv = tknRecv;
  private_path->tknRecvStrided = tknRecvStrided;
  private_path->tknDestroy = tknDestroy;
}
#endif
