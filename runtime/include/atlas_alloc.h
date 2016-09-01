/*
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version. This program is
 * distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU Lesser
 * General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */


#ifndef ATLAS_ALLOC_H
#define ATLAS_ALLOC_H

#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>

//
// Persistent region API
// These are interfaces for creating and managing persistent regions,
// entities that contain persistent data. Once a persistent region is
// created, objects can be allocated out of the region, e.g. by using
// nvm_alloc. Any data not in a persistent region is considered
// transient.
//
#ifdef __cplusplus
extern "C" {
#endif

///
/// @brief Create a named persistent region.
/// @param name Name of the persistent region
/// @param flags access flag (one of O_RDONLY, O_WRONLY, O_RDWR)
/// @return Id of the region created
///
/// This interface does not check for an existing entry with
/// the same name. If a region with the same name already exists, the
/// behavior of the program is undefined.
///
uint32_t NVM_CreateRegion(const char *name, int flags);

///
/// @brief Create a persistent region with the provided name.
/// @param name Name of the persistent region
/// @param flags access flag (one of O_RDONLY, O_WRONLY, O_RDWR)
/// @param is_created Indicator whether the region got created as a
/// result of the call
/// @return Id of the region found or created
///
/// If the region already exists, the existing id of the region is returned.
/// Otherwise a region is created and its newly assigned id returned.
///
uint32_t NVM_FindOrCreateRegion(const char *name, int flags, int *is_created);

///
/// @brief Find the id of a region when it is known to exist already
/// @param name Name of the persistent region
/// @param flags access flag (one of O_RDONLY, O_WRONLY, O_RDWR)
/// @return Id of the region found
///
/// This interface should be used over NVM_FindOrCreateRegion for
/// efficiency reasons if the region is known to exist.  If a region
/// with the provided name does not exist, an assertion failure will
/// occur.
///
uint32_t NVM_FindRegion(const char *name, int flags);

///
/// @brief Delete the region with the provided name.
/// @param name Name of the persistent region
///
/// Use this interface to completely destroy a region. If the region
/// does not exist, an assertion failure will occur.
///
void NVM_DeleteRegion(const char *name);

///
/// @brief Close a persistent region
/// @param rid Region id
///
/// After closing, it won't be available to the calling process
/// without calling NVM_FindOrCreateRegion. The region will stay in
/// NVM even after calling this interface. This interface allows
/// closing a region with normal bookkeeping.
///
void NVM_CloseRegion(uint32_t rid);

///
/// @brief Get the root pointer of the persistent region
/// @param rid Region id
/// @return Root pointer of the region
///
/// The region must have been created already. Currently, only one
/// root is implemented for a given region. The idea is that anything
/// within a region that is not reachable from the root after program
/// termination is assumed to be garbage and can be recycled. During
/// execution, anything within a region that is not reachable from the
/// root or from other _roots_ (in the GC sense) is assumed to be
/// garbage as well.
///
void *NVM_GetRegionRoot(uint32_t rid);

///
/// @brief Set the root pointer of an existing persistent region
/// @param rid Region id
/// @param root The new root of the region
///
void NVM_SetRegionRoot(uint32_t rid, void *root);

///
/// @brief Determines if a memory location is within a region
/// @param ptr Queried address
/// @param sz Size of the location in bytes
/// @return 1 if within the region, otherwise 0    
///
int NVM_IsInRegion(void *ptr, size_t sz);
    
///
/// @brief Determines if the addresses are on different cache lines
///
/// @param p1 First address
/// @param p2 Second address
/// @return Indicates whether the addresses are on different cache
/// lines
///
/// The objects under consideration must not cross cache lines,
/// otherwise this interface is inadequate.
///
int isOnDifferentCacheLine(void *p1, void *p2);

///
/// @brief Determines if a memory location is aligned to a cache line
///
/// @param p Address of memory location under consideration
/// @return Indicates whether the memory location is cache line
/// aligned
///
int isCacheLineAligned(void *p);

///
/// @brief Malloc style interface for allocation from a persistent
/// region
///
/// @param sz Size of location to be allocated
/// @param rid Id of persistent region for allocation
/// @return Address of memory location allocated
///
void *nvm_alloc(size_t sz, uint32_t rid);

///
/// @brief Calloc style interface for allocation from a persistent
/// region
///
/// @param nmemb Number of elements in the array to be allocated
/// @param sz Size of each element
/// @param rid Id of persistent region for allocation
/// @return Pointer to allocated memory
///
void *nvm_calloc(size_t nmemb, size_t sz, uint32_t rid);

///
/// @brief Realloc style interface for allocation from a persistent
/// region
///
/// @param ptr Address of memory block provided
/// @param sz New size of allocation
/// @param rid Id of persistent region for allocation
/// @return Pointer to re-allocated memory
///
void *nvm_realloc(void *ptr, size_t sz, uint32_t rid);

///
/// @brief Deallocation interface for persistent data
///
/// @param ptr Address of memory location to be freed.
///
/// Though the usual use case would be for the location to be in
/// persistent memory, this interface will also work for transient
/// data. The implementation is required to transparently handle
/// this case as well.
///
void nvm_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
