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

TakyonPathAttributes takyonAllocAttributes(bool is_endpointA, bool is_polling, int nbufs_AtoB, int nbufs_BtoA, uint64_t bytes, double timeout, const char *interconnect) {
  // Allocate the appropriate lists
  uint64_t *sender_max_bytes_list = NULL;
  size_t *sender_addr_list = NULL;
  uint64_t *recver_max_bytes_list = NULL;
  size_t *recver_addr_list = NULL;
  int sender_nbufs = is_endpointA ? nbufs_AtoB : nbufs_BtoA;
  int recver_nbufs = is_endpointA ? nbufs_BtoA : nbufs_AtoB;
  if (sender_nbufs > 0) {
    sender_max_bytes_list = calloc(sender_nbufs, sizeof(uint64_t));
    if (sender_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    sender_addr_list = calloc(sender_nbufs, sizeof(uint64_t));
    if (sender_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    for (int i=0; i<sender_nbufs; i++) {
      sender_max_bytes_list[i] = bytes;
    }
  }
  if (recver_nbufs > 0) {
    recver_max_bytes_list = calloc(recver_nbufs, sizeof(uint64_t));
    if (recver_max_bytes_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    recver_addr_list = calloc(recver_nbufs, sizeof(uint64_t));
    if (recver_addr_list == NULL) { fprintf(stderr, "Out of memory.\n"); abort(); }
    for (int i=0; i<recver_nbufs; i++) {
      recver_max_bytes_list[i] = bytes;
    }
  }

  TakyonPathAttributes attrs;
  attrs.is_endpointA           = is_endpointA;
  attrs.is_polling             = is_polling;
  attrs.abort_on_failure       = true;
  attrs.verbosity              = TAKYON_VERBOSITY_ERRORS;
  strncpy(attrs.interconnect, interconnect, TAKYON_MAX_INTERCONNECT_CHARS);
  attrs.path_create_timeout    = timeout;
  attrs.send_start_timeout     = timeout;
  attrs.send_finish_timeout    = timeout;
  attrs.recv_start_timeout     = timeout;
  attrs.recv_finish_timeout    = timeout;
  attrs.path_destroy_timeout   = timeout;
  attrs.send_completion_method = TAKYON_BLOCKING;
  attrs.recv_completion_method = TAKYON_BLOCKING;
  attrs.nbufs_AtoB             = nbufs_AtoB;
  attrs.nbufs_BtoA             = nbufs_BtoA;
  attrs.sender_max_bytes_list  = sender_max_bytes_list;
  attrs.recver_max_bytes_list  = recver_max_bytes_list;
  attrs.sender_addr_list       = sender_addr_list;
  attrs.recver_addr_list       = recver_addr_list;
  attrs.error_message          = NULL;

  return attrs;
}

void takyonFreeAttributes(TakyonPathAttributes attrs) {
  if (attrs.sender_max_bytes_list != NULL) free(attrs.sender_max_bytes_list);
  if (attrs.recver_max_bytes_list != NULL) free(attrs.recver_max_bytes_list);
  if (attrs.sender_addr_list != NULL) free(attrs.sender_addr_list);
  if (attrs.recver_addr_list != NULL) free(attrs.recver_addr_list);
}
