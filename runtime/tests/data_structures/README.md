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

# Data Structures

This directory contains several data structures, each implemented in a
few flavors. An Atlas-based program typically requires:

- Calls to `NVM_Initialize`/`NVM_Finalize`

- Creating/closing a persistent region

- Allocating data structures from a persistent region using `nvm_alloc`

- Writing restart code to find a reusable persistent data structure,
if it exists

- Enclosing updates within a durable section, if required.

- If a program is written with locks, no changes are required to
maintain consistency of updates within these lock-held sections.

Details of the programs in this folder follow:

1. `alarm_clock` is a simple program that sets, updates, cancels a bunch of
alarms. `alarm_clock_nvm` is the corresponding persistent version using
Atlas APIs where the alarm information is kept around for later reuse.

1. `cow_array_list` is a vanilla implementation of a multithreaded
copy-on-write array implementation, inspired by Java
`CopyOnWriteArrayList`. `cow_array_list_nvm` is the corresponding
persistent array implementation using Atlas APIs.

1. `queue` shows a vanilla implementation of a multithreaded queue
(Adapted from Michael & Scott, _Simple, Fast, and Practical
Non-Blocking and Blocking Concurrent Queue Algorithms_, PODC 1996.)
`queue_nvm` is the corresponding persistent queue using Atlas APIs.

1. `sll` is a vanilla serial singly linked list; `sll_ll` is a persistent
serial singly linked list, written using a low-level API that
essentially uses only persistent syncs. `sll_mt` and `sll_mt_ll` are
multithreaded variants. `sll_nvm` is a persistent serial singly linked
list, written using Atlas APIs.

1. `stores` and `stores_nvm` are transient and persistent versions
respectively of an array sweep.

When `<ATLAS_BUILD>/tests/run_quick_test` is invoked, the script
simulates crash/recovery for each of these tests. First, every program
is run to completion, then deliberately crashed, recovered, and then
run again.

In order to invoke any of the tests manually, do the following, for
example for `queue_nvm`:

    $ cd <ATLAS_BUILD>
    $ ./tests/data_structures/queue_nvm

If the program fails for some reason, the recovery process must be
initiated before the program can be run again. For that purpose,
invoke `tools/recover <program executable name>`. For example, for
`queue_nvm`:

    $ ./tools/recover queue_nvm

Once recovery is done, the program can be run again as before. For
example,

    $ ./tests/data_structures/queue_nvm

If all persistent regions need to be cleaned out for some reason,
invoke `tools/clean_mem`. `tools/del_log` and `tools/del_rgn` can be
used to selectively remove a process log and a specific persistent
region respectively.
