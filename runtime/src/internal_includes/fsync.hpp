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

int fsync_paranoid(const char *name) {
    char rp[1+PATH_MAX], *file = (char *) malloc(sizeof(char)*strlen(name)+1);
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
