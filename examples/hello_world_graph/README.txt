Description:
------------
  This example is designed to show how the Takyon extention functions can be used to
  organize collective communications, in this case a single path used to say hello.

  Source files:
    main.c - Loads the graph description file, allocates any needed memory blocks,
             and starts the appropriate threads. This is the framework for the application.
    hello.c - this is the core algorithm which is independent of the application and
              graph framework.

  This more closely represent how a scalable application would be designed.
  For an even better refrence, see the scatter_gather and pipeline examples.


Build:
------
  Mac and Linux:
    > export TAKYON_LIBS=<folder>  // e.g. $HOME/Takyon/API/builds/linux
    > make
    > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    > set TAKYON_LIBS=<folder>     // e.g. c:\takyon\API\build\windows
    > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see the usage options:
      > ./hello

    Multi-threaded:
      > ./hello 0 graph_mt.txt

    Multi-process:
      Terminal 1: > ./hello 0 graph_mp.txt
      Terminal 2: > ./hello 1 graph_mp.txt

  Windows:
    Follow the same as above, but replace "./hello" with "hello"
