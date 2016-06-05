[//]: # ( (c) Copyright 2016 Hewlett Packard Enterprise Development LP         )
[//]: # (                                                                      )
[//]: # ( This program is free software: you can redistribute it and/or modify )
[//]: # ( it under the terms of the GNU Lesser General Public License as       )
[//]: # ( published by the Free Software Foundation, either version 3 of the   )
[//]: # ( License, or (at your option) any later version. This program is      )
[//]: # ( distributed in the hope that it will be useful, but WITHOUT ANY      )
[//]: # ( WARRANTY; without even the implied warranty of MERCHANTABILITY or    )
[//]: # ( FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License )
[//]: # ( for more details. You should have received a copy of the GNU Lesser  )
[//]: # ( General Public License along with this program. If not, see          )
[//]: # ( <http://www.gnu.org/licenses/>.                                      )


# Atlas Runtime APIs

Atlas is a high-level programming environment for non-volatile
memory. In-memory objects can be persisted or made durable with
relatively minor changes to code. There are 2 classes of APIs:
persistent region APIs and consistency APIs. The first class is used
to create/find/delete containers that store persistent data and can be
found in include/atlas_alloc.h. The second class is used to convey
data-consistency information to the system. Currently, this includes a
durable section (demarcated by begin_durable and end_durable) and
classical lock-based critical sections. See `include/atlas_api.h` for
these interfaces. For examples of how to write a persistent program
using Atlas, see `runtime/tests/data_structures/README.md`.

## Building

Atlas "runtime" uses cmake. cmake 3.1 is the minimum required
version. The library build must be in a separate build
directory. Assume that the top-level Atlas runtime directory is
`ATLAS_RUNTIME` and the build directory is `ATLAS_BUILD`. First create
`ATLAS_BUILD`. It is recommended to name this directory with the atlas
build config, e.g. `build-all`, or `build-all-persist`. Invoke cmake
within `ATLAS_BUILD`, passing the path to `ATLAS_RUNTIME` and
any variables needed for the config.  Example:

    $ cd <ATLAS_RUNTIME>
    $ mkdir build-all
    $ cd build-all
    $ cmake ..
    $ make

There are a number of modes for building the runtime. See
`runtime/CMakeLists.txt` for the supported ones. For example, to turn on
Atlas statistics, the cmake config line above should be:

    $ cmake .. -DNVM_STATS=true

To rebuild Atlas runtime when changes are made to the sources,
just invoke `make` again.

# Testing

Persistent memory is simulated using Linux tmpfs, so make sure
`/dev/shm` is available, has enough space, and has `rwx`
permissions. After making, invoke `<ATLAS_BUILD>/tests/run_quick_test`
to do some basic testing. For developers: make sure that
`<ATLAS_RUNTIME>/tools/run_tests` passes before checking in.

# Organization

`ATLAS_RUNTIME` has the following subdirectories:

`include`: contains the headers with exported interfaces. These are
the only headers that should be included in applications.

`src`: the source files

`src/internal_includes`: internal header files

`src/pregion_mgr`: persistent region support

`src/pmalloc`: persistent allocator support

`src/logger`: support for logging updates to persistent memory

`src/consistency`: automatic computation of consistent states and log pruning

`src/cache_flush`: optimized cache line flush support

`src/recover`: support for recovery after a failure

`src/util`: common routines

`tools`: `<ATLAS_RUNTIME>/tools/run_tests` goes through a variety of
build targets and does some basic testing for each of them.

`tests`: directory used for testing. Contains binaries, inputs,
outputs, reference files, etc. See the READMEs in the individual
subdirectories.
