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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FP (void)fprintf

static void bail(char *msg, int line) {
    FP(stderr, "%s:%d: %s: %s\n", __FILE__, line, msg, strerror(errno));
    exit(EXIT_FAILURE);
}
#define BAIL(msg) bail(msg, __LINE__)

static void trim_rightmost_path_component(char *p) {
    char *s = p + strlen(p) - 1;
    if ('/' == *s)
        *s-- = '\0';
    while (s > p && '/' != *s)
        s--;
    *++s = '\0';
}

static int fsync_paranoid(const char *name) {
    char rp[1+PATH_MAX], *file = (char *) malloc(sizeof(char)*(strlen(name)+1));
    strcpy(file, name);
    FP(stderr, "fsync to root '%s'\n", file);
    if (NULL == realpath(file, rp))              BAIL("realpath failed");
    FP(stderr, "     realpath '%s'\n", rp);
    do {
        int fd;
            FP(stderr, "    fsync-ing '%s'\n", rp);
            if (-1 == (fd = open(rp, O_RDONLY)))       BAIL("open failed");
            if (-1 == fsync(fd))                       BAIL("fsync failed");
            if (-1 == close(fd))                       BAIL("close failed");
            trim_rightmost_path_component(rp);
        } while (*rp);
    FP(stderr, "         done\n");
    free(file);
    return 0;
}

static int fsync_dir(const char *name) 
{
    char *file = (char*) malloc(sizeof(char)*(strlen(name)+1));
    strcpy(file, name);
    trim_rightmost_path_component(file);
    FP(stderr, "    fsync-ing '%s'\n", file);
    int fd;
    if (-1 == (fd = open(file, O_RDONLY)))       BAIL("open failed");
    if (-1 == fsync(fd))                       BAIL("fsync failed");
    if (-1 == close(fd))                       BAIL("close failed");
    free(file);
    return 0;
}
