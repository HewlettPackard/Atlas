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



Compiler support for Atlas is currently in the form of an
instrumentation pass (NVM Instrumenter) built on top of the LLVM
compiler (http://llvm.org). Please ensure that LLVM/clang (version 3.6.0 or
above) is installed on your system or added to your PATH. If
required, visit http://llvm.org/releases/download.html to get the latest
binaries for this purpose.

To build the NVM Instrumenter shared library for clang, change
directory to `compiler-plugin` and run `./build_plugin`. The file
`NvmInstrumenter.so` will be created under the subdirectory
`plugin_build`.

Ensure the versions of clang and llvm developer tools (ie llvm-config) are the same.

To use clang with instrumentation, it is recommended to use an
environment variable as used in the Atlas test scripts

    $ACC: clang -Xclang -load -Xclang <path_to_Atlas>/compiler-plugin/plugin_build/NvmInstrumenter.so
    $ACXX: clang++ -Xclang -load -Xclang <path_to_Atlas>/compiler-plugin/plugin_build/NvmInstrumenter.so
