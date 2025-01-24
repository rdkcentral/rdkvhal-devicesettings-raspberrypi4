/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "dshalLogger.h"

static int logflag = 0;

const char *log_type_to_string(int type)
{
    switch (type) {
        case DSHAL_LOG_ERROR:
            return "DSHAL-ERROR";
        case DSHAL_LOG_WARNING:
            return "DSHAL-WARNING";
        case DSHAL_LOG_INFO:
            return "DSHAL-INFO";
        case DSHAL_LOG_DEBUG:
            return "DSHAL-DEBUG";
        default:
            return "DSHAL-DEFAULT";
    }
}

void logger(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}

int getLogFlag(void)
{
    return logflag;
}

void configDSHALLogging(void)
{
    if (access(LOG_CONFIG_FILE, F_OK) == -1) {
        perror("DSHAL configDSHALLogging error accessing debug.ini\n");
        return;
    }
    FILE *file = fopen(LOG_CONFIG_FILE, "r");
    if (!file) {
        perror("DSHAL configDSHALLogging error fopen debug.ini\n");
        return;
    }

    char line[512] = {0};
    const struct {
        const char *nameEnabled;
        const char *nameDisabled;
        int flag;
    } log_levels[] = {
        {"ERROR", "!ERROR", LOG_ERROR},
        {"WARNING", "!WARNING", LOG_WARNING},
        {"INFO", "!INFO", LOG_INFO},
        {"DEBUG", "!DEBUG", LOG_DEBUG}
    };

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        // Check for the LOG.RDK.DSMGR entry
        if (strncmp(line, "LOG.RDK.DSMGR", 13) == 0) {
            for (size_t i = 0; i < sizeof(log_levels) / sizeof(log_levels[0]); ++i) {
                if (strstr(line, log_levels[i].nameEnabled)) {
                    logflag |= log_levels[i].flag;
                } else if (strstr(line, log_levels[i].nameDisabled)) {
                    logflag &= ~log_levels[i].flag;
                }
            }
            break;
        }
    }
    fclose(file);
    printf("DSHAL configDSHALLogging logflag: 0x%x\n", logflag);
    fprintf(stderr, "DSHAL configDSHALLogging logflag: 0x%x\n", logflag);
}

// Make constructor run the logging config when module loaded into memory.
static void __attribute__((constructor)) initDSHALLogging(void)
{
    configDSHALLogging();
}

