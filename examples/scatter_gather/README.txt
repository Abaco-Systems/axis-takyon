Description:
------------
  This example is designed to show how collective calls could be organized from a
  set of Takyon paths.

  This example has some very revealing features to emphasize the flexibility of Takyon:
    - Mixes inter-threaded with inter-process paths in a single app.
    - Allows paths to be grouped into collective calls (e.g. scatter, gather). In this
      case the same paths are used by scatter and gather, but that's not a requirement.
    - The master side allocates a block of memory to hold all of the sender side
      scatter data in a single contiguous block. This memory block is used by all paths.
    - The master side allocates a block of memory to hold all of the receiver side
      gather data in a single contiguous block. This memory block is used by all paths.
      For this to work with an 'Mmap' interconnect, it means the memory block must be
      a named memory mapped. To see the details, look for USE_MEMORY_MAP_FOR_GATHER in
      collective_utils.c
    - Adding more paths is simple, just add more to the dataflow text file, and run
      more slave executables if needed. No compiling is needed, which means this app
      is scalable.

  The executable has two modes: master and slave. One master should exist. The master
  will create all the inter-thread paths. For any inter-process or inter-processor
  paths, a separate slave executable needs to run.

  Master algorithm:
    1. Scatter to all slave endpoints (scatter data is evenly distributed so each
       slave gets unique data).
    2. Gather from all slave endpoints (gathered data is contiguous).
  Slave algorithm:
    1. Recv data (amount if data received is total_bytes/npaths)
    2. Send data (amount if data received is total_bytes/npaths)

  Source files:
    main.c - Loads the dataflow file and starts the appropriate threads. This is a framework
             for the application. This file could eventually be automatically generated
             by a dataflow manager tool since it's mostly independent of the core algorithm.
    collective_utils.c - This encapsulates the details of the collective groups for scatter
                         and gather. This could eventually be replaced by Takyon collective
                         functions, yet to be determined.
    scatter_gather.c - This is the heart of the algorithm. Its design is similar to how most
                       high level communication API would be used, such as MPI.

  There are some supported command line arguments used to tune the application
  as needed. To see all the supported options, just run the executable without
  any arguments.

  Dataflow test 1 description:
    This test uses 4 paths. The first 3 paths use the 'Memcpy' interconnect which is
    an inter-thread communication, and the 4th path uses the 'Socket -local'
    interconnect which is an inter-process communication.
    The application executable needs to be run from two shells.
    One executable needs to be run as the master. The master will create a master
    thread to do the scattering and gathering, and will also create slave threads
    for the 3 'Memcpy' paths.
    The second executable is a slave process for the 'Socket' path.

  Dataflow test 2 description:
    This test uses 6 paths. The first 2 paths use the 'Memcpy' interconnect which is
    an inter-thread communication, the 3rd and 4th paths use the 'Mmap' interconnect
    which is an inter-process communication, and the 5th and 6th paths use the 'Socket'
    interconnect which is an inter-process and inter-processor communication.
    The application executable needs to be run from five shells.
    One executable needs to be run as the master. The master will create a master
    thread to do the scattering and gathering, and will also create slave threads
    for the 2 'Memcpy' paths.
    Four more executables need to run as slave processes for the 'Mmap' and 'Socket' paths.

  Create your own dataflow text files to experiment with as needed.


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

    Dataflow Test 1:
      Terminal 1:
        > ./scatter_gather dataflow_test1.txt
      Terminal 2:
        > ./scatter_gather dataflow_test1.txt -pathIndex 3

    Dataflow Test 2:
      Terminal 1:
        > ./scatter_gather dataflow_test2.txt
      Terminal 2:
        > ./scatter_gather dataflow_test2.txt -pathIndex 2
      Terminal 3:
        > ./scatter_gather dataflow_test2.txt -pathIndex 3
      Terminal 4:
        > ./scatter_gather dataflow_test2.txt -pathIndex 4
      Terminal 5:
        > ./scatter_gather dataflow_test2.txt -pathIndex 5

  Windows:
    Follow the same as above, but replace "./scatter_gather" with "scatter_gather"
