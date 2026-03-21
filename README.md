## XGAINFO
---
 XGAINFO is a small, command-line utility for MSDOS that will detect and display any information about any XGA cards installed in a Microchannel bus system (i.e. most IBM PS/2s). ISA XGA cards are not supported. This is still a work in progress and I may add more functionality to it. Might be helpful for troubleshooting issues getting your XGA card to work.

It is written mostly in C but I also used a lot of inline assembly because I'm trying to learn x86 asm. 

# Command line options
---
By default xgainfo will display the same basic info that the reference disk does. You can use additional arguments to expose more data. 

|Argument|Options|Details|Example|
|--------|-------|-------|-------|
|-h      |       |Displays command line arguments||
|-f      |filename|Send output to a file instead of the screen|xgainfo -f output.txt|
|-p      |       |Pauses output every 25 lines so output does not immediately scroll off the screen|
|-s      |params|Show certain information specified by params|xgainfo -s pix (See table below)|
|-a      |       |Displays all information available||


|Option|Details|
|------|-------|
|p|Show contents of POS registers|
|i|Show contents of I/O registers|
|x|Show contents of indexed I/O registers|

1.Multiple params can be specified (do not use spaces): xgainfo -s ixp  

2.Combining multiple arguments into one (ex: xgainfo -ap) is not supported

# Building the source
---

The source was compiled in Borland Turbo C++ 3.0 in PCDOS 7 running inside 86Box 4.2 (because it supports emulating the XGA and IBM PS/2 hardware). There is only one source file (xgainfo.c) and relies only on (the MSDOS version of) the C standard library. Just open the file in the IDE and compile to run it.

# Licensing
---
I'm releasing the source under GPL v2 so code that expands on this code stays public. The XGA card is cool and we should preserve the knowledge of how to program for it.
