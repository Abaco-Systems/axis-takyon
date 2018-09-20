Description:
------------
  This example is designed to show how the Takyon extention functions can be used to
  organize collective communications, in this case a reduce collective is used to
  reduce values from all threads to a single thread. The reduction function is defined
  by the application, and in this case uses a max function.

  This example has features to emphasize the flexibility of Takyon and its extentions:
    - The reduce and barrier collective use the same set of paths, minimizing resources.
    - simplifies how to start an app: <executable> <process_id> <graph_description_file>
    - Mixes inter-threaded with inter-process paths in a single app.
    - Allows Takyon paths to be grouped into collective calls (e.g. a reduce and barrier).
    - Adding more paths is simple, just update the graph description file, and run
      more executables if needed. No compiling is needed, which means this app
      is scalable.

  The executable has one type of processing thread:
    1. Reduce the data from all nodes (vector max operation).
    2. Optionally sends the results to all threads.
    2. Do a barrier to synchronize the next reduction.

  Source files:
    main.c - Loads the graph description file, allocates any needed memory blocks,
             and starts the appropriate threads. This is the framework for the application.
    reduce.c - This is the heart of the algorithm. Notice this source code is
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
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./reduce

    One process, multiple threads:
      Terminal 1:
        > ./reduce 0 graph_mt.txt -scatter

    All in one OS, multiple processes and multiple threads:
      Terminal 1:
        > ./reduce 0 graph_mp.txt -scatter
      Terminal 2:
        > ./reduce 1 graph_mp.txt -scatter
      Terminal 3:
        > ./reduce 2 graph_mp.txt -scatter

  Windows:
    Follow the same as above, but replace "./reduce" with "reduce"
