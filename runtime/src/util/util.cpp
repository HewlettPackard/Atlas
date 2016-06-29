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
 

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <pwd.h>
#include <assert.h>

#include "util.hpp"

#ifndef _NVDIMM_PROLIANT
    static const char mountpath[]="/dev/shm/";
#else
    static const char mountpath[]="/mnt/nvm/pmem0/";
#endif

char *NVM_GetRegionTablePath()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    const char *usr_name = getpwuid(geteuid())->pw_name;
    char *s = (char*) malloc(
        (strlen(mountpath) + strlen(usr_name) +
         strlen("/__nvm_region_table") + 1) * sizeof(char));
    sprintf(s, "%s%s/__nvm_region_table", mountpath, usr_name);
    return s;
}
    
char *NVM_GetUserDir()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    const char *usr_name = getpwuid(geteuid())->pw_name;
    char *s = (char*) malloc(
        (strlen(mountpath) + strlen(usr_name) + 1) * sizeof(char));
    sprintf(s, "%s%s", mountpath, usr_name);
    return s;
}
    
char *NVM_GetLogDir()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#ifdef PMM_OS
    return "/dev/pmmfs";
#else
    const char *usr_name = getpwuid(geteuid())->pw_name;
    char *s = (char*) malloc(
        (strlen(mountpath) + strlen(usr_name) + strlen("/regions") + 1) *
        sizeof(char));
    sprintf(s, "%s%s/regions", mountpath, usr_name);
    return s;

#endif
}

void NVM_CreateUserDir()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    int status;
    const char *usr_dir = NVM_GetUserDir();
    struct stat buf;
    if (stat(usr_dir, &buf))
    {
        status =
            mkdir(usr_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        assert(!status);
    }
    free((void*)usr_dir);
}

void NVM_CreateLogDir()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#ifdef PMM_OS
    system("mkdir -p /dev/pmmfs");
#else
    int status;
    const char *usr_dir = NVM_GetUserDir();
    const char *log_dir = NVM_GetLogDir();
    struct stat buf;
    if (stat(usr_dir, &buf))
    {
        status =
            mkdir(usr_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        assert(!status);

        status =
            mkdir(log_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        assert(!status);
    }
    else
    {
        if (stat(log_dir, &buf))
        {
            status = mkdir(
                log_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
            assert(!status);
        }
    }
    free((void*)log_dir);
    free((void*)usr_dir);
#endif
}

#ifdef PMM_OS
char *NVM_GetFullyQualifiedRegionName(const char * name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(strlen("/dev/pmmfs/")+strlen(name) < 2*MAXLEN+1);
    char *s = (char*) malloc(2*MAXLEN*sizeof(char));
    sprintf(s, "/dev/pmmfs/%s", name);
    return s;
}
#else
char *NVM_GetFullyQualifiedRegionName(const char * name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    const char *usr_name = getpwuid(geteuid())->pw_name;
    char *s = (char*) malloc(
        (strlen(mountpath) + strlen(usr_name) + strlen("/regions/") +
         strlen(name) + 1) * sizeof(char));
    sprintf(s, "%s%s/regions/%s", mountpath, usr_name, name);
    return s;
}
#endif

char *NVM_GetLogRegionName()
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    extern const char *__progname;
    char *s = (char*) malloc(
        (strlen("logs_") + strlen(__progname) + 1) * sizeof(char));
    sprintf(s, "logs_%s", __progname);
    return s;
}

char *NVM_GetLogRegionName(const char * name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    char *s = (char*) malloc(
        (strlen("logs_") + strlen(name) + 1) * sizeof(char));
    sprintf(s, "logs_%s", name);
    return s;
}

bool NVM_doesLogExist(const char *log_path_name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
    assert(log_path_name);
    struct stat buf;
    int status = stat(log_path_name, &buf);
    return status == 0;
}

void NVM_qualifyPathName(char *s, const char *name)
{
#ifdef _FORCE_FAIL
    fail_program();
#endif
#ifdef PMM_OS
    sprintf(s, "/dev/pmmfs/%s", name);
#else
    sprintf(s, "%sregions/%s", mountpath, name);
#endif
}

template<> uint32_t SimpleHashTable<SetOfInts>::Size_ = 1024;



