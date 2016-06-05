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

 

################
REGION TESTS
################
Each script in this directory calls region functions in the atlas
api and performs some minor work (function test in region_test.h which
creates a linked list). Change the macros WORK and ITERATIONS to alter
the amount of work.

The order in which the atlas alloc api is called is in the name eg:
focclose: FindOrCreateRegion, CloseRegion
createclosefocclose: CreateRegion, CloseRegion, FindOrCreateRegion,
CloseRegion 
finddelete: FindRegion, DeleteRegion

The test script test_region_instr, runs these tests up to 100 times
depending on expected behaviour.  There are three behaviours in two
classes:

Runnable tests which either abort, create regions or find regions.
Non runnable tests which can only abort.

The test programs are put into one of the two classes depending on if
they should run in the first place.  If they are runnable the programs
either execute one of the three behaviours when run again.

To add a test, edit CMakeLists.txt, and in test_region_instr add the
test name to the respective local arrays.  Eg creating a test focdelete
which is runnable and creates regions must be added to runnable_tests
and tests_create. A test finddelete which is nonrunnable and aborts,
must only be added to nonrunnable_tests.

test_region_instr also does an instrumentation check, verifying
correctness of the compiler plugin. It checks the number of stores,
lock acquires and releases for focdelete against expected values.
If the atlas build dir is not under runtime the environment variable
PLUGIN should be set to the path of NvmInstrumenter.so.
ie PLUGIN=/path/to/NvmInstrumenter.so

If neither PLUGIN is set or the build directory is not under runtime,
no instrumentation checking wil be attempted.

To run the test script do ./test_region_instr false. Run with
./test_region_instr true to see debug information if there are errors,
for example.
