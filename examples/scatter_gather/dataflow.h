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

#ifndef _dataflow_h_
#define _dataflow_h_

#include "collective_utils.h"

typedef struct {
  PathDetails *interconnect_list;
  bool         is_polling;
  int          npaths;
  int          nbufs;
  uint64_t     nbytes;
  int          ncycles;
} DataflowDetails;

typedef struct {
  int path_index;
  DataflowDetails *dataflow;
} FunctionInfo;

#endif
