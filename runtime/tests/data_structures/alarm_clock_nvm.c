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

/*
 * Alarm clock
 * Functionality:
 * - Setting an alarm with certain parameters
 * - Resetting an alarm
 * - Ring an alarm, resetting it
 * - Search for an alarm and change its parameters
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// Atlas includes
#include "atlas_alloc.h"
#include "atlas_api.h"

#define AC_TABLE_SIZE 32
#define AC_TABLE_MASK (AC_TABLE_SIZE - 1)
#define AC_TABLE_ENTRY(hour, min)                                              \
    (AlarmClockTab + (((hour) + (min)) & AC_TABLE_MASK))
#define AC_LOCK(hour, min)                                                     \
    (AlarmClockLockTab + (((hour) + (min)) & AC_TABLE_MASK))

typedef struct AlarmClockInfo {
    uint8_t hour;
    uint8_t min;
    uint8_t mode;
    uint8_t repeat_factor;
    char name[128];
    struct AlarmClockInfo *next;
} AlarmClockInfo;

AlarmClockInfo **AlarmClockTab;
pthread_mutex_t AlarmClockLockTab[AC_TABLE_SIZE];

int set_alarms = 0;
int update_alarms = 0;
int cancelled_alarms = 0;
int failed_cancel_alarms = 0;

// ID of Atlas persistent region
uint32_t alarm_clock_rgn_id;

AlarmClockInfo *CreateNewInfo(uint8_t hour, uint8_t min, uint8_t mode,
                              uint8_t repeat_factor, char *name) {
    AlarmClockInfo *ninfo =
        (AlarmClockInfo *)nvm_alloc(sizeof(AlarmClockInfo), alarm_clock_rgn_id);
    ninfo->hour = hour;
    ninfo->min = min;
    ninfo->mode = mode;
    ninfo->repeat_factor = repeat_factor;
    strcpy(ninfo->name, name);
    ninfo->next = NULL;
    return ninfo;
}

static inline pthread_mutex_t *GetLock(uint8_t hour, uint8_t min) {
    return AC_LOCK(hour, min);
}

static inline AlarmClockInfo *GetHeader(uint8_t hour, uint8_t min) {
    return *AC_TABLE_ENTRY(hour, min);
}

int add_or_update_alarm(uint8_t hour, uint8_t min, uint8_t mode,
                        uint8_t repeat_factor, char *name) {
    pthread_mutex_t *bucket_mtx = GetLock(hour, min);
    pthread_mutex_lock(bucket_mtx);
    AlarmClockInfo *header = GetHeader(hour, min);
    AlarmClockInfo *temp = header;
    // Check whether an existing entry should be updated
    while (temp) {
        if (temp->hour == hour && temp->min == min) {
            temp->mode = mode;
            temp->repeat_factor = repeat_factor;
            strcpy(temp->name, name);
            pthread_mutex_unlock(bucket_mtx);
            return 0;
        }
        temp = temp->next;
    }
    // A new entry is inserted
    AlarmClockInfo *naci = CreateNewInfo(hour, min, mode, repeat_factor, name);
    naci->next = header;
    *AC_TABLE_ENTRY(hour, min) = naci;
    pthread_mutex_unlock(bucket_mtx);
    return 1;
}

// Return 1 if found and removed, otherwise return 0
int cancel_alarm(uint8_t hour, uint8_t min, int play) {
    pthread_mutex_t *bucket_mtx = GetLock(hour, min);
    pthread_mutex_lock(bucket_mtx);
    AlarmClockInfo *cand = GetHeader(hour, min);
    AlarmClockInfo *prev = NULL;
    while (cand) {
        if (cand->hour == hour && cand->min == min) {
            // Optionally, play the alarm here before essentially removing it
            // ...

            if (play) {
                ; // sound an alarm
            }
            if (!prev) {
                *AC_TABLE_ENTRY(hour, min) = cand->next;
            } else {
                prev->next = cand->next;
            }
            nvm_free(cand);
            pthread_mutex_unlock(bucket_mtx);
            return 1;
        }
        prev = cand;
        cand = cand->next;
    }
    pthread_mutex_unlock(bucket_mtx);
    return 0;
}

void print_alarms() {
    fprintf(stderr, "---------------\n");
    for (int i = 0; i < AC_TABLE_SIZE; ++i) {
        AlarmClockInfo *aci = AlarmClockTab[i];
        while (aci) {
            fprintf(stderr, "%d %d %d %d %s\n", aci->hour, aci->min, aci->mode,
                    aci->repeat_factor, aci->name);
            aci = aci->next;
        }
    }
}

void *play_alarms() {
    int8_t hour = 23;
    int8_t min;
    int status;
    while (hour >= 0) {
        min = 59;
        while (min >= 0) {
            status = cancel_alarm(hour, min, 1);
            if (status) {
                ++cancelled_alarms;
            } else {
                ++failed_cancel_alarms;
            }
            min -= 1;
        }
        --hour;
    }
    return NULL;
}

void initialize() {
    void *rgn_root = NVM_GetRegionRoot(alarm_clock_rgn_id);
    if (rgn_root) {
        AlarmClockTab = (AlarmClockInfo **)rgn_root;
    } else {
        AlarmClockTab = (AlarmClockInfo **)nvm_alloc(
            AC_TABLE_SIZE * sizeof(AlarmClockInfo *), alarm_clock_rgn_id);
    }
}

int main() {
    struct timeval tv_start;
    struct timeval tv_end;

    gettimeofday(&tv_start, NULL);

    pthread_t th;

    // Initialize Atlas
    NVM_Initialize();
    // Create an Atlas persistent region
    alarm_clock_rgn_id = NVM_FindOrCreateRegion("alarm_clock", O_RDWR, NULL);
    // This contains the Atlas restart code to find any reusable data
    initialize();
    // Set the root of the Atlas persistent region
    NVM_SetRegionRoot(alarm_clock_rgn_id, AlarmClockTab);

    pthread_create(&th, 0, (void *(*)(void *))play_alarms, 0);

    uint8_t hour = 0;
    uint8_t min;
    int ret;

#ifdef _FORCE_FAIL
    srand(time(NULL));
    int randval = rand() % 24;
#endif

    while (hour < 24) {
#ifdef _FORCE_FAIL
        if (hour == randval) exit(0);
#endif
        min = 0;
        while (min < 60) {
            ret = add_or_update_alarm(hour, min, 0, 0, "");
            if (ret) {
                ++set_alarms;
            } else {
                ++update_alarms;
            }
            min += 1;
        }
        hour += 1;
    }
    pthread_join(th, NULL);
    // print_alarms();

    // Close the Atlas persistent region
    NVM_CloseRegion(alarm_clock_rgn_id);
    // Optionally print Atlas stats
#ifdef NVM_STATS
    NVM_PrintStats();
#endif
    // Atlas bookkeeping
    NVM_Finalize();

    fprintf(stderr,
            "Set = %d Updated = %d Cancelled = %d Failed canceling = %d\n",
            set_alarms, update_alarms, cancelled_alarms, failed_cancel_alarms);
    gettimeofday(&tv_end, NULL);
    fprintf(stderr, "time elapsed %ld us\n",
            tv_end.tv_usec - tv_start.tv_usec +
                (tv_end.tv_sec - tv_start.tv_sec) * 1000000);
    return 0;
}
