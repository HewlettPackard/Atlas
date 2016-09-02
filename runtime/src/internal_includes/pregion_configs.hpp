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
 

#ifndef PREGION_CONFIGS_HPP
#define PREGION_CONFIGS_HPP

namespace Atlas {

typedef uint32_t region_id_t;

const uint32_t kDCacheLineSize_ = 64;
const uint32_t kMaxlen_ = kDCacheLineSize_;
    
const uint64_t kByte_ = 1024;
#ifdef _NVDIMM_PROLIANT
    const uint64_t kPRegionSize_ = 1 * kByte_ * kByte_ * kByte_; /* 1GB */
#else
    const uint64_t kPRegionSize_ = 4 * kByte_ * kByte_ * kByte_; /* 4GB */
#endif    
const uint32_t kMaxNumPRegions_ = 100;
const uint32_t kNumArenas_ = 64;
const uint32_t kArenaSize_ = kPRegionSize_ / kNumArenas_;
const uint32_t kMaxFreeCategory_ = 128;
const uint32_t kInvalidPRegion_ = kMaxNumPRegions_;
const uint32_t kMaxBits_ = 48;
const uint64_t kPRegionsBase_ = 
    (((uint64_t)1 << (kMaxBits_ - 1)) - (kPRegionSize_ * kMaxNumPRegions_))/2;

} // namespace Atlas

#endif
