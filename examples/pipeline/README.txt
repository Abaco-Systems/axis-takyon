Description:
------------
  This example is designed to show how the Takyon extention functions can be used to
  organize collective communications, in this case a pipeline of paths used to create
  one long path.

  This example has features to emphasize the flexibility of Takyon and its extentions:
    - simplifies how to start an app: <executable> <process_id> <graph_description_file>
    - Mixes inter-threaded with inter-process paths in a single app.
    - Allows Takyon paths to be grouped into collective calls (e.g. a pipeline).
    - Adding more paths is simple, just update the graph description file, and run
      more executables if needed. No compiling is needed, which means this app
      is scalable.

  The executable has one type of processing thread, but with three sections:
    - Start of pipe: creates the initial data and sends it to the next thread.
    - Intermediate pipe thread: receives data from the previous thread, adds one,
      and sends it to the next thread.
    - End of pipe: receives data from the previous thread and validates for correctness.

  Source files:
    main.c - Loads the graph description file, allocates any needed memory blocks,
             and starts the appropriate threads. This is the framework for the application.
    pipelie.c - This is the heart of the algorithm. Notice this source code is
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
      > ./pipeline

    One process, multiple threads:
      Terminal 1:
        > ./pipeline 0 graph_mt.txt

    All in one OS, multiple processes and multiple threads:
      Terminal 1:
        > ./pipeline 0 graph_mp.txt
      Terminal 2:
        > ./pipeline 1 graph_mp.txt
      Terminal 3:
        > ./pipeline 2 graph_mp.txt

  Windows:
    Follow the same as above, but replace "./pipeline" with "pipeline"
