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
 

#ifndef _RECOVER_H
#define _RECOVER_H

#include <map>

using namespace std;
using namespace Atlas;

//namespace Atlas {
    
// There is a one-to-one mapping between a release and an acquire log
// entry, unless the associated lock is a rwlock
typedef multimap<LogEntry*, pair<LogEntry*,int /* thread id */> > MapR2A;
typedef MapR2A::const_iterator R2AIter;
typedef map<int /* tid */, LogEntry* /* last log replayed */> Tid2Log;
typedef map<LogEntry*, bool> MapLog2Bool;
typedef map<uint32_t, bool> MapInt2Bool;
typedef map<LogEntry*, LogEntry*> MapLog2Log;

void R_Initialize(const char *name);
void R_Finalize(const char *name);
LogStructure *GetLogStructureHeader();
void CreateRelToAcqMappings(LogStructure*);
void AddToMap(LogEntry*,int);
void Recover();
void Recover(int);
void MarkReplayed(LogEntry*);
bool isAlreadyReplayed(LogEntry*);

//} // namespace Atlas

#endif
