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

#ifndef __DSHALLOGGER_H
#define __DSHALLOGGER_H

#define LOG_CONFIG_FILE "/etc/debug.ini"

#define LOG_ERROR   (1 << 0)
#define LOG_WARNING (1 << 1)
#define LOG_INFO    (1 << 2)
#define LOG_DEBUG   (1 << 3)

enum {
    DSHAL_LOG_ERROR = 0,
    DSHAL_LOG_WARNING,
    DSHAL_LOG_INFO,
    DSHAL_LOG_DEBUG
};

void logger(const char *format, ...);
int getLogFlag(void);
void configDSHALLogging(void);

#define log_generic(log_flag, format, ...) \
    do                                     \
{                                      \
    if (getLogFlag() & (log_flag))     \
    {                                  \
        logger(format, ##__VA_ARGS__); \
    }                                  \
} while (0)

// public use logging macros
#define hal_err(format, ...)    log_generic(LOG_ERROR, "[DSHAL:%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define hal_warn(format, ...)   log_generic(LOG_WARNING, "[DSHAL:%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define hal_info(format, ...)   log_generic(LOG_INFO, "[DSHAL:%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define hal_dbg(format, ...)    log_generic(LOG_DEBUG, "[DSHAL:%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)

#endif // __DSHALLOGGER_H

