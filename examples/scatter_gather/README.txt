Description:
------------
  This example is designed to show how the Takyon extention functions can be used to
  organize scatter and gather collective communications. This will be helpful to
  those used to MPI collective calls.

  This example has features to emphasize the flexibility of Takyon and its extentions:
    - simplifies how to start an app: <executable> <process_id> <graph_description_file>
    - Mixes inter-threaded with inter-process paths in a single app.
    - Allows Takyon paths to be grouped into collective calls (e.g. scatter, gather).
      In this case the same paths are used by scatter and gather for an efficient use
      of resources, but that's not a requirement.
    - The parent side allocates a block of memory to hold all of the sender side
      scatter data in a single contiguous block. Each scatter receiver get a unique
      block of the data.
    - The parent side allocates a block of memory to hold all of the receiver side
      gather data in a single contiguous block. This memory block is used by all paths.
      For this to work with an 'Mmap' interconnect, it means the memory block must be
      a named memory mapped. The graph description file defines an memory block that
      is allocated from a named memory map. This memory map is then referenced in the
      arguments for creating a Mmap interconnect path.
    - Adding more paths is simple, just update the graph description file, and run
      more executables if needed. No compiling is needed, which means this app
      is scalable.

  The executable has two types of processing threads: parent and child. Only one
  parent should exist. There can be 1 or more children. The graph description file
  defines how many processes and threads there are and what their IDs are.

  Parent algorithm:
    1. Scatter, from a contiguous block, to all child endpoints.
    2. Gather from all child endpoints.
    3. Verify received data is correct.
  Child algorithm:
    1. Recv scattered data.
    3. Add 1 to each byte.
    2. Send data to be gathered.

  Source files:
    main.c - Loads the graph description file, allocates any needed memory blocks,
             and starts the appropriate threads. This is the framework for the application.
    scatter_gather.c - This is the heart of the algorithm. Notice this source code is
             compleltly scalable; i.e. if more paths are added in the graph description
             file, this source code remains unaffected.

  There are some supported command line arguments used to tune the application
  as needed. To see the supported options, just run the executable without
  any arguments.

  Create your own graph description files to experiment with as needed.


Build:
------
  Mac and Linux:
    Terminal:
      > make
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./scatter_gather

    One process, multiple threads:
      > ./scatter_gather 0 graph_mt.txt

    All in one OS, multiple processes and multiple threads:
      Terminal 1: > ./scatter_gather 0 graph_mp.txt
      Terminal 2: > ./scatter_gather 1 graph_mp.txt
      Terminal 3: > ./scatter_gather 2 graph_mp.txt

  Windows:
    Follow the same as above, but replace "./scatter_gather" with "scatter_gather"
