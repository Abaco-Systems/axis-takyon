Description:
------------
  This example is designed to show how the Takyon can transfer between various combinations
  of CUDA and CPU buffers on a single communication path.

  This uses the Takyon graph description files so one application can handle any path
  locality: inter-thread, inter-process, inter-processor.

  Source files:
    main.c - Loads the graph description file, allocates any needed CPU or CUDA memory blocks,
             and starts the appropriate threads. This is the framework for the application.
    hello.c - This is the core algorithm which is independent of the application and
              graph frameworks.


Build:
------
  Mac and Linux:
    > make WITH_CUDA=Yes
    > make WITH_CUDA=Yes USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    > nmake -f windows.Makefile WITH_CUDA=Yes


Run:
----
  Mac and Linux:
    To see the usage options:
      > ./hello

    Multi-threaded:
      > ./hello 0 graph_mt.txt
      > ./hello 0 graph_mt_shared.txt

    Multi-process:
      Terminal 1: > ./hello 0 graph_mp.txt
      Terminal 2: > ./hello 1 graph_mp.txt

      Terminal 1: > ./hello 0 graph_mp_shared.txt
      Terminal 2: > ./hello 1 graph_mp_shared.txt

  Windows:
    Follow the same as above, but replace "./hello" with "hello"
