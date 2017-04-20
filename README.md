# Atlas: Programming for Persistent Memory
[![Build Status](https://travis-ci.org/HewlettPackard/Atlas.svg?branch=master)](https://travis-ci.org/HewlettPackard/Atlas)

Data in persistent memory survives certain tolerated events such as
process termination, OS reboots/crashes, and power
failures. Persistent memory is assumed to be directly accessible with
CPU loads and stores. This kind of programming is relevant on new
servers with NVDIMMs as well as future machines with
non-volatile memory such as memristors or 3D XPoint. Atlas provides
high level APIs that allow the programmer to persist data in a
fault-tolerant manner and reuse it later on. Any program which has
reusable data that can be exploited to achieve a faster restart or a
restart from an intermediate program point is a candidate for this
paradigm.

A persistent memory allocator is provided for identifying data that
should be preserved regardless of machine shutdowns or failures. By
conforming to certain programming idioms and APIs, programmers can
automatically obtain failure-resilience of persistent data. The 
programming model with implementation details can be found in the
[OOPSLA 2014 paper on Atlas](http://dl.acm.org/citation.cfm?id=2660224).

The current implementation supports
POSIX threads but the implementation for C/C++11 threads should be similar.
Linux tmpfs is currently used to simulate persistent
memory. Hence, persistent data in this implementation survives process
crashes but not OS shutdowns/panics and power failures. However, the
APIs and the implementation are ready for all of the above
failures. The intention is to allow programmers to write code in a
programming style that is ready for upcoming persistent memory based
systems.

This software is currently experimental, see `COPYING` for license
terms. Contributions and feedback are very welcome.

## What is Included?

APIs are provided for creation and management of persistent
regions which are implemented on top of memory mapped files. Support
for a persistent memory allocator is provided. In essence, a
programmer is able to identify data structures that must be maintained
in a persistent manner. The goal of Atlas is to ensure that persistent
data are updated in a consistent manner regardless of failures.

The current implementation has two primary components: a
compiler-plugin and a runtime library. The programmer writes
multithreaded code, possibly using locks for synchronization, and puts
data in persistent regions as required. This code is passed through
the plugin at compile-time that results in calls to the runtime
library at appropriate program points. When this program is run,
automatic failure-atomicity (all-or-nothing) of updates to persistent
data structures is provided. If a failure occurs, recovery must be
initiated to ensure that persistent data structures are consistent.

## Persistent Region APIs

A preview is provided here. See `runtime/include/atlas_alloc.h` for the
actual interfaces.

A programmer needs to create one or more named persistent regions,
entities that hold everything persistent. The interface
**_NVM_FindOrCreateRegion_** or a variant can be used for this purpose. If a
region with the provided name exists, a handle to the region is
returned. Otherwise a region is created and its handle is
returned. Interfaces to close or delete a region are available.

To populate a persistent region, memory must be dynamically allocated
from that persistent region using **_nvm_alloc_** (or a variant) that has a
malloc-style interface. The region identifier must be provided so as
to identify the persistent region intended. An **_nvm_free_** is provided
for deallocation purposes.

Management of persistent regions and the contained data together
identify the persistent objects used by a program. Care must be taken
to ensure that all valid data within a persistent region is reachable
from the persistent root of the region. Use the interface
**_NVM_SetRegionRoot_** for this purpose.

## Consistency APIs

See `runtime/include/atlas_api.h` for the actual interfaces.

Persistent data must be kept consistent regardless of failures. The
programmer needs to call **_NVM_Initialize_** and **_NVM_Finalize_**
to start and stop Atlas support. Additionally, Atlas needs to know
code sections where program invariants are modified. If the program is
multithreaded and written using locks for synchronization, Atlas
automatically infers boundaries of regions where it must preserve
failure-atomicity (all-or-nothing) of updates to persistent
memory. Optionally, the programmer can demarcate sections of code with
calls to **_nvm_begin_durable_** and **_nvm_end_durable_** to identify
a durable or failure-atomic section of code. Note that no isolation
among threads is provided by a durable section. In contrast, if
persistent data is modified within a critical section, the critical
section provides both isolation among threads and durability to
persistent memory.

## Restart Code

A program might want to reuse data within a persistent region. For
this purpose, after finding a region handle, use the interface
**_NVM_GetRegionRoot_** to access the reachable data. Instead of
starting from scratch, this data can be reused to essentially restart
from where the region was left off the last time around.

That's all, as far as Atlas APIs are concerned. Compared to a
transient program, the idea is to write persistent memory programs
with as few changes as possible.

## Organization

- The APIs for this model are in `runtime/include`. [API doc here](http://hewlettpackard.github.io/Atlas/runtime/doc/atlas__api_8h.html).
- Instructions on how to build the compiler-plugin are in
`compiler-plugin/README.md`.
- Instructions on how to build the runtime are in `runtime/README.md`.
- For example programs using Atlas, see `runtime/tests`.
- The Atlas library sources are in `runtime/src`.

## Dependencies

* Currently, we support only x86-64 CPUs
* We assume Linux OS. Linux tmpfs must be supported. Testing has been
  done on RedHat and Ubuntu.
* We assume modern C/C++ compilers in the build environment that must
  support C/C++11.
* The default compiler used in the build is clang. Testing has been
  done with version 3.6.0 or later. The instrumentation support is
  currently available only with clang/LLVM. The runtime should build
  with any compiler supporting C/C++11 though clang is preferred for
  uniformity purposes.
* cmake version 3.1 or later
* boost library
* bash 4.0

For Ubuntu 16.04, these dependencies can be installed with:

    sudo apt-get install llvm clang cmake libboost-graph-dev

* ruby (for certain test scripts), see **Installing Ruby** at [gorails](https://gorails.com/setup/ubuntu/16.04) for instructions.

## Discuss
Questions, feedback, comments are welcome on our public [mailing list](https://groups.google.com/forum/#!forum/atlas-discuss). Subscribe by using the Google Groups web interface or by sending an email with subject “subscribe” to atlas-discuss+subscribe [AT] googlegroups.com.


## Reference

Dhruva R. Chakrabarti, Hans-J. Boehm, and Kumud Bhandari. 2014.
[Atlas: leveraging locks for non-volatile memory consistency](http://dl.acm.org/citation.cfm?id=2660224).
In _Proceedings of the 2014 ACM International Conference on Object Oriented
Programming Systems Languages & Applications_ (OOPSLA '14). ACM, New
York, NY, USA, 433-452.
