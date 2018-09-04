#ifndef _scatter_gather_h_
#define _scatter_gather_h_

void masterTask(TakyonDataflow *dataflow, ThreadDesc *thread_desc, int ncycles);
void slaveTask(TakyonDataflow *dataflow, ThreadDesc *thread_desc, int ncycles);

#endif
