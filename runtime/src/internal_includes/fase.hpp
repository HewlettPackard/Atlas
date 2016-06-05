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
 

#ifndef FASE_HPP
#define FASE_HPP

#include "log_mgr.hpp"

namespace Atlas {

// Represent a failure atomic section of code.    
struct FASection {
    explicit FASection(LogEntry *first, LogEntry *last) 
        : First{first},
        Last{last},
        Next{nullptr},
        IsDeleted{false} {}
    FASection() = delete;
    FASection(const FASection&) = delete;
    FASection(FASection&&) = delete;
    FASection& operator=(const FASection&) = delete;
    FASection& operator=(FASection&&) = delete;
    
    LogEntry *First;
    LogEntry *Last;
    FASection *Next;
    bool IsDeleted;
};

} // namespace Atlas

#endif
