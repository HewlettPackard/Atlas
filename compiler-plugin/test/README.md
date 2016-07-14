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

# Instrumentation Tests

`test_instr` performs instrumentation checks on different instructions, verifying
that the compiler plugin performs the correct instrumentations, both type and quantity.

## Usage

To run the test script do `./test_instr false`. Run with
`./test_instr true` to see debug information, if there are errors
for example.

## Requirements

The LLVM toolchain must be built debug with assertions enabled. This is needed in order to
run opt with -stats, more on the LLVM Statistic class
[here](http://llvm.org/docs/ProgrammersManual.html#statistic).

The compiler plugin `NvmInstrumenter.so` must also be built (with assertions).
If the compiler plugin is built outside of `plugin_build/`,
set environment variable PLUGIN to its location.

If these are not met the script will return with a non zero exit code.

## Adding New Tests

Both C or C++ tests are allowed.

Tests are placed in this directory `compiler-plugin/test`.

To add a new test `test.c` a corresponding `test.ref` must be created
and placed under `compiler-plugin/test/test_refs`.

A `.ref` file is used to check that the expected instrumentation
occured. These are generated with clang and opt. Run the following from this directory.

    $ clang -c -emit-llvm test.c &> /dev/null
    $ opt -load ../plugin_build/NvmInstrumenter.so -NvmInstrumenter -stats < test.bc > /dev/null 2> test.ref; rm test.bc
    $ sed -i '/Number of/!d' test.ref

View the contents of `test.ref`. If the correct number of instrumentations have occurred,
move `test.ref` to `test_refs/`.
