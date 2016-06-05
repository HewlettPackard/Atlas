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
 

#ifndef _UTIL_H
#define _UTIL_H

#include <stdint.h>
#include <string.h>
#include <map>
#include <set>
#include <atomic>

typedef std::pair<uint64_t,uint64_t> UInt64Pair;
class CmpUInt64
{
public:
    bool operator()(const UInt64Pair & c1, const UInt64Pair & c2) const
        {
            return (c1.first < c2.first) && (c1.second < c2.second);
        }
};
typedef std::map<UInt64Pair,uint32_t,CmpUInt64> MapInterval;

typedef std::pair<void*,size_t> AddrSizePairType;
class CmpAddrSizePair
{
public:
    bool operator()(const AddrSizePairType & c1, const AddrSizePairType & c2) const 
        {
            if ((uintptr_t)c1.first < (uintptr_t)c2.first) return true;
            if ((uintptr_t)c1.first > (uintptr_t)c2.first) return false;
            if (c1.second < c2.second) return true;
            return false;
        }
};
typedef std::set<AddrSizePairType, CmpAddrSizePair> SetOfPairs;

// This is not thread safe. Currently ok to call from recovery code but
// not from anywhere else.
inline void InsertToMapInterval(
    MapInterval *m, uint64_t e1, uint64_t e2, uint32_t e3)
{
    (*m)[std::make_pair(e1,e2)] = e3;
}

// This can be called from anywhere.
inline MapInterval::const_iterator FindInMapInterval(
    const MapInterval & m, const uint64_t e1, const uint64_t e2)
{
    return m.find(std::make_pair(e1, e2));
}

inline void InsertSetOfPairs(SetOfPairs *setp, void *addr, size_t sz)
{
    (*setp).insert(std::make_pair(addr, sz));
}

inline SetOfPairs::const_iterator FindSetOfPairs(
    const SetOfPairs & setp, void *addr, size_t sz)
{
    return setp.find(std::make_pair(addr, sz));
}

typedef std::set<uint64_t> SetOfInts;

template <class ElemType>
class ElemInfo
{
public:
    ElemInfo(void *addr, const ElemType & elem)
        { Addr_ = addr; Elem_ = new ElemType(elem); Next_ = 0; }
private:
    void *Addr_;
    std::atomic<ElemType*> Elem_;
    ElemInfo *Next_;
};

template <class ElemType>
class SimpleHashTable
{
public:
    SimpleHashTable(uint32_t size=0)
        {
            if (size) Size_ = size;
            Tab_ = new std::atomic<ElemInfo<ElemType>*> [Size_];
            memset((void*)Tab_, 0, Size_*sizeof(std::atomic<ElemInfo<ElemType>*>));
        }
    void Insert(void*, const ElemType &);
    ElemInfo<ElemType> *Find(void*);
private:
    std::atomic<ElemInfo<ElemType>*> *Tab_;
    static uint32_t Size_;

    std::atomic<ElemInfo<ElemType>*> *GetTableEntry(void *addr)
        { return (Tab_ + (((uintptr_t(addr)+128) >> 3) & (Size_-1))); }

    ElemInfo<ElemType> *GetElemInfoHeader(void *addr)
        {
            std::atomic<ElemInfo<ElemType>*> *entry = GetTableEntry(addr);
            return (*entry).load(std::memory_order_acquire);
        }
    std::atomic<ElemInfo<ElemType>*> *GetPointerToElemInfoHeader(void *addr)
        {
            return GetTableEntry(addr);
        }
};

char *NVM_GetRegionTablePath();
char *NVM_GetUserDir();
char *NVM_GetLogDir();
void NVM_CreateUserDir();
void NVM_CreateLogDir();
char *NVM_GetFullyQualifiedRegionName(const char *name);

// Derive the log name from the program name for the running process
char *NVM_GetLogRegionName(); 
char *NVM_GetLogRegionName(const char *prog_name);
bool NVM_doesLogExist(const char *log_path_name);
void NVM_qualifyPathName(char *s, const char *name);

#endif
