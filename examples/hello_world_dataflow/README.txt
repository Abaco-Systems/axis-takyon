Description:
------------
  A very simple example to create a single path and send messages in each direction.

  This example is done using the very flexiby dataflow utility functions. This allows
  the dataflow to be defined in a file so the application does not need to be re-compiled
  if a change is made to the dataflow or interconnects.

  There are two C files:
  main.c - this sets up the application and dataflow framework.
  hello.c - this is the core algorithm which is independent of the application and
            dataflow framework.

  This more closely represent how a scalable application would be designed.
  For an even better refrence, see the scatter_gather example.


Build:
------
  Mac and Linux:
    > make
  Windows:
    > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see the usage options:
      > ./hello

    Multi-threaded:
      > ./hello 0 dataflow_mt.txt

    Multi-process:
      Terminal 1:
        > ./hello 0 dataflow_mp.txt
      Terminal 2:
        > ./hello 1 dataflow_mp.txt

  Windows:
    Follow the same as above, but replace "./hello" with "hello"
