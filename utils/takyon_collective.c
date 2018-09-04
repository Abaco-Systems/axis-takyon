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

ScatterSrc *takyonScatterSrcInit(int npaths, TakyonPath **path_list) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_list == NULL) {
    fprintf(stderr, "%s(): path_list is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<npaths; i++) {
    if (path_list[i] == NULL) {
      fprintf(stderr, "%s(): path_list[%d] is NULL\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
  }
  ScatterSrc *group = (ScatterSrc *)calloc(1, sizeof(ScatterSrc));
  if (group == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  group->path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (group->path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  group->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    group->path_list[i] = path_list[i];
  }
  return group;
}

ScatterDest *takyonScatterDestInit(int npaths, int path_index, TakyonPath *path) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_index < 0 || path_index >= npaths) {
    fprintf(stderr, "%s(): path_index must be >= 0 and < npaths\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path == NULL) {
    fprintf(stderr, "%s(): path is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  ScatterDest *group = (ScatterDest *)calloc(1, sizeof(ScatterDest));
  if (group == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  group->npaths = npaths;
  group->path_index = path_index;
  group->path = path;
  return group;
}

void takyonScatterSend(ScatterSrc *group, int buffer, uint64_t *nbytes_list, uint64_t *soffset_list, uint64_t *doffset_list) {
  for (int i=0; i<group->npaths; i++) {
    bool timed_out;
    bool ok = takyonSend(group->path_list[i], buffer, nbytes_list[i], soffset_list[i], doffset_list[i], &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): path index %d timed out.\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): path index %d failed with error:\n%s\n", __FUNCTION__, i, group->path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
  // If non blocking, then wait for completion
  for (int i=0; i<group->npaths; i++) {
    if (group->path_list[i]->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
      bool timed_out;
      bool ok = takyonSendTest(group->path_list[i], buffer, &timed_out);
      if (ok && timed_out) {
        fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      if (!ok) {
        fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, group->path_list[i]->attrs.error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

void takyonScatterRecv(ScatterDest *group, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret) {
  bool timed_out;
  bool ok = takyonRecv(group->path, buffer, nbytes_ret, offset_ret, &timed_out);
  if (ok && timed_out) {
    fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (!ok) {
    fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, group->path->attrs.error_message);
    exit(EXIT_FAILURE);
  }
}

void takyonScatterSrcFinalize(ScatterSrc *group) {
  free(group->path_list);
  free(group);
}

void takyonScatterDestFinalize(ScatterDest *group) {
  free(group);
}

GatherSrc *takyonGatherSrcInit(int npaths, int path_index, TakyonPath *path) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_index < 0 || path_index >= npaths) {
    fprintf(stderr, "%s(): path_index must be >= 0 and < npaths\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path == NULL) {
    fprintf(stderr, "%s(): path is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  GatherSrc *group = (GatherSrc *)calloc(1, sizeof(GatherSrc));
  if (group == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  group->npaths = npaths;
  group->path_index = path_index;
  group->path = path;
  return group;
}

GatherDest *takyonGatherDestInit(int npaths, TakyonPath **path_list) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_list == NULL) {
    fprintf(stderr, "%s(): path_list is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<npaths; i++) {
    if (path_list[i] == NULL) {
      fprintf(stderr, "%s(): path_list[%d] is NULL\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
  }
  GatherDest *group = (GatherDest *)calloc(1, sizeof(GatherDest));
  if (group == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  group->path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (group->path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  group->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    group->path_list[i] = path_list[i];
  }
  return group;
}

void takyonGatherSend(GatherSrc *group, int buffer, uint64_t nbytes, uint64_t soffset, uint64_t doffset) {
  bool timed_out;
  bool ok = takyonSend(group->path, buffer, nbytes, soffset, doffset, &timed_out);
  if (ok && timed_out) {
    fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (!ok) {
    fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, group->path->attrs.error_message);
    exit(EXIT_FAILURE);
  }
  // If non blocking, then wait for completion
  if (group->path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    ok = takyonSendTest(group->path, buffer, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, group->path->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonGatherRecv(GatherDest *group, int buffer, uint64_t *nbytes_list_ret, uint64_t *offset_list_ret) {
  for (int i=0; i<group->npaths; i++) {
    bool timed_out;
    bool ok = takyonRecv(group->path_list[i], buffer, &nbytes_list_ret[i], &offset_list_ret[i], &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, group->path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonGatherSrcFinalize(GatherSrc *group) {
  free(group);
}

void takyonGatherDestFinalize(GatherDest *group) {
  free(group->path_list);
  free(group);
}
