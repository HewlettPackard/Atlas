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


#ifndef ATLAS_ALLOC_CPP_H
#define ATLAS_ALLOC_CPP_H

// Forward declarations
namespace Atlas 
{
    class PRegion;
}

///
/// @brief Get a handle to a persistent region
/// @param rid Region identifier
/// @return Pointer to the corresponding persistent region
///
/// Currently, this interface is to be used by a client only for the
/// placement new operations
///
Atlas::PRegion *NVM_GetRegion(uint32_t rid);

///
/// @brief Object allocation for C++
/// @param sz Allocation size
/// @param rgn Pointer to the region to serve the allocation from
/// @return Pointer to memory allocated
///
/// This interface overloads the C++ placement new operator. The
/// corresponding delete operation is NVM_Destroy.
///
void* operator new(size_t sz, Atlas::PRegion *rgn) noexcept;

///
/// @brief Array form allocation for C++, type must have explicit
/// destructor 
/// @param sz Allocation size
/// @param rgn Pointer to the region to serve the allocation from
/// @return Pointer to memory allocated
///
/// This interface overloads the array form C++ placement new
/// operator. The type of the array elements must have an explicit
/// destructor. The corresponding delete operation is
/// NVM_Destroy_Array.
///
void* operator new[](size_t sz, Atlas::PRegion *rgn) noexcept;

///
/// @brief Object destruction for C++
/// @param ptr Pointer to memory to be deallocated
///
/// This interface should be called for destroying an object that was
/// created from a persistent region using the single-object new
/// operator. The implementation calls the destructor followed by
/// actual deallocation. This interface can also be called for
/// destroying an object that was created using the default
/// single-object new operator. If the latter is the case, the
/// implementation detects this situation and turns around to call the
/// default single-object delete operator. This interface must not be
/// called for deallocating an object created in any other way, e.g. a
/// placement new operator where the placement address is not within a
/// persistent region. 
///
template <class T> static inline void NVM_Destroy(T *ptr)
{
    if (!ptr) return;
    if (!NVM_IsInRegion(ptr, 1 /* dummy, since size unknown */)) {
        delete ptr;
        return;
    }
    ptr->~T();
    void nvm_delete(void*);
    nvm_delete(ptr);
}

///
/// @brief Array form destruction for C++, type must have explicit
/// destructor 
/// @param ptr Pointer to memory to be deallocated
///
/// This interface should be called for destroying an array of objects
/// that was created from a persistent region using the array-form new
/// operator. The implementation calls the destructors for all objects
/// of the array followed by actual deallocation. Note that the type
/// of the array elements must have an explicit destructor for this
/// interface to work correctly. This interface can also be called for
/// destroying an array of objects that was created using the default
/// array-form new operator. If the latter is the case, the
/// implementation detects this situation and turns around to call the
/// default array-form delete operator. This interface must not be
/// called for deallocating an object created in any other way. 
///
template <class T> static inline void NVM_Destroy_Array(T *ptr)
{
    if (!ptr) return;
    if (!NVM_IsInRegion(ptr, 1 /* dummy, since size unknown */)) {
        delete [] ptr;
        return;
    }
    char *delete_ptr = reinterpret_cast<char*>(ptr) - sizeof(size_t);
    size_t count = *reinterpret_cast<size_t*>(delete_ptr);
    for (int i=count-1; i>=0; --i) (ptr+i)->~T();
    void nvm_delete(void*);
    nvm_delete(delete_ptr);
}

#endif
