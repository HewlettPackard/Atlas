# Makalu : NVRAM Memory Allocator

Makalu is a failure-resilient, thread-safe memory allocator
that can be used for the allocation of byte-addressable 
non-volatile memory (NVRAM). Makalu is designed to restart 
across execution cycles using durable metadata 
stored in NVRAM. While it takes advantage of 
transient DRAM, CPU caches, and registers, 
Makalu internal crash consistency mechanism is developed 
with the assumption that only the data in NVRAM survive a 
crash. It uses memory fences, and selective cache 
line flushing instructions to guarantee the 
timely visibility of updates to its persistent metadata.

Makalu is expected to be used together with other NVRAM
programming libraries (NVMPLs) such Atlas, and Mnemosyne. In such
use-case scenario, the NVMPL ensures the consistency of the
user data stored in heap, while Makalu ensures the consistency
of the heap structure and the absence of failure-induced memory
leaks. Makalu can be used in the absence of NVMPL as a stand-alone
allocator. However, in such use-case, it is upto the programmer to
ensure the consistency of the data stored in persistent heap
across failures and restarts. We only recommend this for experts
in persistent programming.

Makalu has two distinct phases in which it operates. In an **online phase**,
a user can allocate and deallocate 
persistent memory after proper initialization. After an ungraceful shutdown, 
Makalu is expected to start in **offline phase** for recovery and offline 
garbage collection.


##Makalu API##



