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
 

#ifndef PMALLOC_UTIL_HPP
#define PMALLOC_UTIL_HPP

namespace Atlas {

class PMallocUtil {
public:
    static void set_default_tl_curr_arena(region_id_t rid)
        { TL_CurrArena_[rid] = kNumArenas_; /* set to invalid */}
    
    static void set_tl_curr_arena(region_id_t rid, uint32_t val)
        { TL_CurrArena_[rid] = val; }
    
    static uint32_t get_tl_curr_arena(region_id_t rid)
        { return TL_CurrArena_[rid]; }

    static uint32_t get_tl_next_arena(region_id_t rid)
        { return (get_tl_curr_arena(rid) + 1) % kNumArenas_; }
    
    static bool is_valid_tl_curr_arena(region_id_t rid)
        { return TL_CurrArena_[rid] != kNumArenas_; }
            
    static void set_cache_line_size(uint32_t sz)
        { CacheLineSize_ = sz; }

    static void set_cache_line_mask(uintptr_t mask)
        { CacheLineMask_ = mask; }

    static uint32_t get_cache_line_size()
        { assert(CacheLineSize_ != UINT32_MAX); return CacheLineSize_; }

    static uintptr_t get_cache_line_mask()
        { assert(CacheLineMask_ != UINTPTR_MAX); return CacheLineMask_; }
    
    static void *mem2ptr(void *mem)
        { return static_cast<void*>(
              static_cast<char*>(mem) + get_metadata_size()); }
    
    static void *ptr2mem(void *ptr)
        { return static_cast<void*>(
              static_cast<char*>(ptr) - get_metadata_size()); }
            
    static size_t get_alignment() 
        { return 2*sizeof(size_t); }

    static size_t get_alignment_mask() 
        { return get_alignment() - 1; }

    static size_t get_metadata_size() 
        { return 2*sizeof(size_t); }

    static size_t get_smallest_actual_alloc_size() 
        { return get_metadata_size(); }

    static size_t get_actual_alloc_size(size_t sz) 
        { return (sz + get_metadata_size() + get_alignment_mask()) &
                ~get_alignment_mask(); }

    static size_t get_requested_alloc_size_from_mem(void *mem) 
        { return *(static_cast<size_t*>(mem)); }

    static size_t get_requested_alloc_size_from_ptr(void *ptr) 
        { void *mem = ptr2mem(ptr);
            return *(static_cast<size_t*>(mem)); }

    static size_t *get_is_allocated_ptr_from_mem(void *mem) 
        { return reinterpret_cast<size_t*>(
              static_cast<char*>(mem) + sizeof(size_t)); }
    
    static size_t *get_is_allocated_ptr_from_ptr(void *ptr) 
        { void *mem = ptr2mem(ptr);
            return reinterpret_cast<size_t*>(
                static_cast<char*>(mem) + sizeof(size_t)); }

    static bool is_mem_allocated(void *mem) 
        { return *get_is_allocated_ptr_from_mem(mem) == true; }

    static bool is_ptr_allocated(void *ptr) 
        { return *get_is_allocated_ptr_from_ptr(ptr) == true; }

    static uint32_t get_next_bin_number(uint32_t bin_number) 
        {
            assert(bin_number && "Non-zero bin number!");
            assert(!(bin_number % get_alignment()) && "Unaligned bin number!");
            return bin_number + get_alignment();
        }

    static bool is_cache_line_aligned(void *p) 
        { return (reinterpret_cast<uintptr_t>(p) &
                  PMallocUtil::get_cache_line_mask()) ==
                reinterpret_cast<uintptr_t>(p); }

    static bool is_on_different_cache_line(void *p1, void *p2)
        { return (reinterpret_cast<uintptr_t>(p1) &
                  PMallocUtil::get_cache_line_mask()) !=
                (reinterpret_cast<uintptr_t>(p2) &
                 PMallocUtil::get_cache_line_mask()); }
    
    static uint32_t get_bin_number(size_t sz) 
        {
            return (sz < kMaxFreeCategory_) ? 
                (sz + get_alignment_mask()) & ~get_alignment_mask() :
                kMaxFreeCategory_;
        }
private:
    static uint32_t CacheLineSize_;
    static uintptr_t CacheLineMask_;
    static thread_local uint32_t TL_CurrArena_[kMaxNumPRegions_];
};

} // namespace Atlas
    
#endif
