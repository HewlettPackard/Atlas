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



# REGION TESTS

## Usage

To run the test script do `./test_region false`. Run with
`./test_region true` to see debug information, if there are errors
for example.

## Test Structure

Each program in this directory calls region functions in the Atlas
API and performs some minor work (function `test` in `region_test.h` which
creates a linked list). Change the macros `WORK` and `NUM_NODES` to alter
the amount of work.

The order in which the Atlas region api is called is in the filename, e.g.:
- `focclose.c`: `FindOrCreateRegion`, `CloseRegion`
- `createclosefocclose.c`: `CreateRegion`, `CloseRegion`, `FindOrCreateRegion`,
 `CloseRegion`
- `finddelete.c`: `FindRegion`, `DeleteRegion`

The test script `test_region`, runs these tests up to 100 times
depending on expected behaviour.  There are three behaviours in two
classes:

1. Runnable tests which either _abort_, _create regions_ or _find regions_.
2. Non runnable tests which can only _abort_.

The test programs are put into one of the two classes depending on if
they should run in the first place.  If they are runnable the programs
either execute one of the three behaviours when run again.

To add a test, edit `CMakeLists.txt`, and in `test_region` add the
test name to the respective local arrays in function `run_tests`.  E.g.
creating a test `focdelete` which is runnable and creates regions must be
added to `runnable_tests` and `tests_create`. A test `finddelete` which is
nonrunnable and aborts, must only be added to `nonrunnable_tests`.

