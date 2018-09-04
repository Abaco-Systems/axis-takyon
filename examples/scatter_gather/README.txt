Description:
------------
  This example is designed to show how collective calls could be organized from a
  set of Takyon paths. This is simplified by using Takyon utility functions to read
  a dataflow specification file that defines the threads, processes, memory blocks,
  paths, and collective groups.

  This example has some very revealing features to emphasize the flexibility of Takyon:
    - Mixes inter-threaded with inter-process paths in a single app.
    - Allows paths to be grouped into collective calls (e.g. scatter, gather). In this
      case the same paths are used by scatter and gather, but that's not a requirement,
      but allows for an efficient use of resources.
    - The master side allocates a block of memory to hold all of the sender side
      scatter data in a single contiguous block. This memory block is used by all paths.
    - The master side allocates a block of memory to hold all of the receiver side
      gather data in a single contiguous block. This memory block is used by all paths.
      For this to work with an 'Mmap' interconnect, it means the memory block must be
      a named memory mapped. The dataflow description file defines an memory block that
      is allocated from a named memory map. This memory map is then reference in the
      arguments for creating a Mmap interconnect path.
    - Adding more paths is simple, just update the dataflow text file, and run
      more slave executables if needed. No compiling is needed, which means this app
      is scalable.

  The executable has two type for processing threads: master and slave. Only one
  master should exist. There can be 1 or more slaves. The dataflow description file
  defines how many processes there are and what their IDs are.

  Master algorithm:
    1. Scatter, from a contiguous block, to all slave endpoints.
    2. Gather from all slave endpoints.
    3. Verify received data is correct.
  Slave algorithm:
    1. Recv scattered data.
    3. Add 1 to each byte.
    2. Send data to be gathered.

  Source files:
    main.c - Loads the dataflow file, allocates any needed memory blocks, and starts the
             appropriate threads. This is the framework for the application.
    scatter_gather.c - This is the heart of the algorithm. Notice this source code is
             compleltly scalable; i.e. no matter what the dataflow description file
             define (threads, processes, memory blocks, interconnects for each path, etc),
             this source code does not need to change. Its design is similar to how most
             high level communication APIs would be used, such as MPI.

  There are some supported command line arguments used to tune the application
  as needed. To see the supported options, just run the executable without
  any arguments.

  Create your own dataflow description files to experiment with as needed.


Build:
------
  Mac and Linux:
    Terminal:
      > make
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./scatter_gather

    One process, multiple threads:
      Terminal 1:
        > ./scatter_gather 0 dataflow_mt.txt

    All in one OS, multiple processes and multiple threads:
    Dataflow Test 2:
      Terminal 1:
        > ./scatter_gather 0 dataflow_mp.txt
      Terminal 2:
        > ./scatter_gather 1 dataflow_mp.txt
      Terminal 3:
        > ./scatter_gather 2 dataflow_mp.txt

  Windows:
    Follow the same as above, but replace "./scatter_gather" with "scatter_gather"
