---
layout: page
title: About
permalink: /about/
---


# Atlas: Programming for Persistent Memory

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

The programming model with implementation details can be found in the
[OOPSLA 2014 paper on
Atlas](http://www.labs.hpe.com/people/dhruva_chakrabarti/atlas_oopsla2014.pdf).
The current implementation supports
POSIX threads. Implementation for C/C++11 threads should be similar.

The implementation currently uses Linux tmpfs to simulate persistent
memory. Hence, persistent data in this implementation survives process
crashes but not OS shutdowns/panics and power failures. However, the
APIs and the implementation are ready for all of the above
failures. The intention is to allow programmers to write code in a
programming style that is ready for upcoming persistent memory based
systems.

This software is currently experimental, see [`COPYING`](https://github.com/HewlettPackard/Atlas/blob/master/COPYING) for license
terms. Contributions and feedback are very welcome.


See project [README](https://github.com/HewlettPackard/Atlas/blob/master/README.md) to learn more.
