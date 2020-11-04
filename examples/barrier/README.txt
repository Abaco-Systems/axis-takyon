Description:
------------
  This example is designed to show how the Takyon extention functions can be used to
  organize collective communications, in this case a pipeline of paths used to create
  one long path and a tree based barrier is used to synchronize the processing of the
  pipeline processing.

  This example has features to emphasize the flexibility of Takyon and its extentions:
    - simplifies how to start an app: <executable> <process_id> <graph_description_file>
    - Mixes inter-threaded with inter-process paths in a single app.
    - Allows Takyon paths to be grouped into collective calls (e.g. a pipeline and barrier).
    - Adding more paths is simple, just update the graph description file, and run
      more executables if needed. No compiling is needed, which means this app
      is scalable.

  The executable has one type of processing thread:
    1. Move data on the pipeline.
    2. Do a barrier before moving data on the pipeline again.

  Source files:
    main.c - Loads the graph description file, allocates any needed memory blocks,
             and starts the appropriate threads. This is the framework for the application.
    barrier.c - This is the heart of the algorithm. Notice this source code is
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
      > export TAKYON_LIBS=<folder>  // e.g. $HOME/Takyon/API/builds/linux
      > make
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > set TAKYON_LIBS=<folder>     // e.g. c:\takyon\API\build\windows
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./barrier

    One process, multiple threads:
      > ./barrier 0 graph_mt.txt

    All in one OS, multiple processes and multiple threads:
      Terminal 1: > ./barrier 0 graph_mp.txt
      Terminal 2: > ./barrier 1 graph_mp.txt
      Terminal 3: > ./barrier 2 graph_mp.txt

  Windows:
    Follow the same as above, but replace "./barrier" with "barrier"
