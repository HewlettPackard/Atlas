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
 

#ifndef PREGION_MGR_UTIL_HPP
#define PREGION_MGR_UTIL_HPP

#include <map>
#include <utility>

#include "pregion_configs.hpp"

namespace Atlas {

class PRegionExtentMap {
public:
    typedef std::pair<intptr_t,intptr_t> IntPtrPair;
    class CmpIntPtr {
    public:
        bool operator()(
            const IntPtrPair & c1, const IntPtrPair & c2) const {
            return (c1.first < c2.first) &&
                (c1.second < c2.second);
        }
    };
    typedef std::map<IntPtrPair,uint32_t,CmpIntPtr> MapInterval;

    PRegionExtentMap() = default;
    PRegionExtentMap(const PRegionExtentMap& from) {
        MapInterval::const_iterator ci_end = from.Extents_.end();
        for (MapInterval::const_iterator ci =
                 from.Extents_.begin(); ci != ci_end; ++ ci)
            insertExtent(ci->first.first, ci->first.second, ci->second);
    }

    void insertExtent(intptr_t first, intptr_t last, uint32_t id)
        { Extents_[std::make_pair(first,last)] = id; }

    void deleteExtent(intptr_t first, intptr_t last, uint32_t id) {
        MapInterval::iterator ci = Extents_.find(
            std::make_pair(first,last));
        if (ci != Extents_.end()) Extents_.erase(ci);
    }

    uint32_t findExtent(intptr_t first, intptr_t last) const {
        MapInterval::const_iterator ci = Extents_.find(
            std::make_pair(first,last));
        if (ci != Extents_.end()) return ci->second;
        return kInvalidPRegion_;
    }
private:
    MapInterval Extents_;
};
    
} // namespace Atlas
            
#endif


