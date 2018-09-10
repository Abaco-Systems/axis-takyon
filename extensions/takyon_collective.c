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

TakyonCollectiveOne2One *takyonOne2OneInit(int npaths, TakyonPath **src_path_list, TakyonPath **dest_path_list) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (src_path_list == NULL) {
    fprintf(stderr, "%s(): src_path_list is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (dest_path_list == NULL) {
    fprintf(stderr, "%s(): dest_path_list is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  TakyonCollectiveOne2One *collective = (TakyonCollectiveOne2One *)calloc(1, sizeof(TakyonCollectiveOne2One));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->src_path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (collective->src_path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->dest_path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (collective->dest_path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    collective->src_path_list[i] = src_path_list[i];
    collective->dest_path_list[i] = dest_path_list[i];
  }
  return collective;
}

void takyonOne2OneFinalize(TakyonCollectiveOne2One *collective) {
  free(collective->src_path_list);
  free(collective->dest_path_list);
  free(collective);
}

TakyonScatterSrc *takyonScatterSrcInit(int npaths, TakyonPath **path_list) {
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
  TakyonScatterSrc *collective = (TakyonScatterSrc *)calloc(1, sizeof(TakyonScatterSrc));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (collective->path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    collective->path_list[i] = path_list[i];
  }
  return collective;
}

TakyonScatterDest *takyonScatterDestInit(int npaths, int path_index, TakyonPath *path) {
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
  TakyonScatterDest *collective = (TakyonScatterDest *)calloc(1, sizeof(TakyonScatterDest));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  collective->path_index = path_index;
  collective->path = path;
  return collective;
}

void takyonScatterSend(TakyonScatterSrc *collective, int buffer, uint64_t *nbytes_list, uint64_t *soffset_list, uint64_t *doffset_list) {
  for (int i=0; i<collective->npaths; i++) {
    bool timed_out;
    bool ok = takyonSend(collective->path_list[i], buffer, nbytes_list[i], soffset_list[i], doffset_list[i], &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): path index %d timed out.\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): path index %d failed with error:\n%s\n", __FUNCTION__, i, collective->path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
  // If non blocking, then wait for completion
  for (int i=0; i<collective->npaths; i++) {
    if (collective->path_list[i]->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
      bool timed_out;
      bool ok = takyonSendTest(collective->path_list[i], buffer, &timed_out);
      if (ok && timed_out) {
        fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      if (!ok) {
        fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path_list[i]->attrs.error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

void takyonScatterRecv(TakyonScatterDest *collective, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret) {
  bool timed_out;
  bool ok = takyonRecv(collective->path, buffer, nbytes_ret, offset_ret, &timed_out);
  if (ok && timed_out) {
    fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (!ok) {
    fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path->attrs.error_message);
    exit(EXIT_FAILURE);
  }
}

void takyonScatterSrcFinalize(TakyonScatterSrc *collective) {
  free(collective->path_list);
  free(collective);
}

void takyonScatterDestFinalize(TakyonScatterDest *collective) {
  free(collective);
}

TakyonGatherSrc *takyonGatherSrcInit(int npaths, int path_index, TakyonPath *path) {
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
  TakyonGatherSrc *collective = (TakyonGatherSrc *)calloc(1, sizeof(TakyonGatherSrc));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  collective->path_index = path_index;
  collective->path = path;
  return collective;
}

TakyonGatherDest *takyonGatherDestInit(int npaths, TakyonPath **path_list) {
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
  TakyonGatherDest *collective = (TakyonGatherDest *)calloc(1, sizeof(TakyonGatherDest));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (collective->path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    collective->path_list[i] = path_list[i];
  }
  return collective;
}

void takyonGatherSend(TakyonGatherSrc *collective, int buffer, uint64_t nbytes, uint64_t soffset, uint64_t doffset) {
  bool timed_out;
  bool ok = takyonSend(collective->path, buffer, nbytes, soffset, doffset, &timed_out);
  if (ok && timed_out) {
    fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (!ok) {
    fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path->attrs.error_message);
    exit(EXIT_FAILURE);
  }
  // If non blocking, then wait for completion
  if (collective->path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    ok = takyonSendTest(collective->path, buffer, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonGatherRecv(TakyonGatherDest *collective, int buffer, uint64_t *nbytes_list_ret, uint64_t *offset_list_ret) {
  for (int i=0; i<collective->npaths; i++) {
    bool timed_out;
    bool ok = takyonRecv(collective->path_list[i], buffer, &nbytes_list_ret[i], &offset_list_ret[i], &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonGatherSrcFinalize(TakyonGatherSrc *collective) {
  free(collective);
}

void takyonGatherDestFinalize(TakyonGatherDest *collective) {
  free(collective->path_list);
  free(collective);
}
