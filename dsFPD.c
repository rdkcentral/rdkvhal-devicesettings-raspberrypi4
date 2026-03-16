/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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

/* Enable POSIX/GNU extensions for O_NOFOLLOW and pthread_condattr_setclock */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>

#include "dsFPD.h"
#include "dsFPDTypes.h"
#include "dshalLogger.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/***
 * RaspberryPi 4 specific implementation of the Device Settings HAL for LED Indicator.
 * We shall be using the GREEN LED on the RaspberryPi 4 as the RDK Status LED for this implementation.
 * The APIs in this module will ONLY control the state of this LED.
 * No 7-segment display or multi-color LED support, hence related APIs will return dsERR_OPERATION_NOT_SUPPORTED.
 */

// RPi4 has only one ACT LED which is used as the status LED.
// See README.md for details on supported states and patterns for this LED.
static const dsFPDLedState_t gSupportedLEDStates = (dsFPDLedState_t)(
	(1u << dsFPD_LED_DEVICE_ACTIVE) |
	(1u << dsFPD_LED_DEVICE_STANDBY) |
	(1u << dsFPD_LED_DEVICE_WPS_CONNECTING) |
	(1u << dsFPD_LED_DEVICE_WPS_CONNECTED) |
	(1u << dsFPD_LED_DEVICE_WPS_ERROR) |
	(1u << dsFPD_LED_DEVICE_FACTORY_RESET) |
	(1u << dsFPD_LED_DEVICE_USB_UPGRADE) |
	(1u << dsFPD_LED_DEVICE_SOFTWARE_DOWNLOAD_ERROR)
);

static dsFPDLedState_t gCurrentLEDState = dsFPD_LED_DEVICE_NONE;
static dsFPDBrightness_t gCurrentBrightness = dsFPD_BRIGHTNESS_MAX;
static pthread_mutex_t gLEDStateMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gLEDPatternCond;
static bool gCondVarReady = false;
static pthread_t gLEDPatternThread;
static bool gIsFPDInitialized = false;
static bool gLEDPatternThreadRunning = false;
static bool gLEDPatternThreadStop = false;
static bool gExitCleanupRegistered = false;
static dsFPDState_t gFPState = dsFPD_STATE_OFF;
static bool gCustomBlinkActive = false;
static unsigned int gCustomBlinkDurationMs = 0;
static unsigned int gCustomBlinkIterations = 0;
static uint64_t gCustomBlinkRequestId = 0;
static dsFPDLedState_t gCustomBlinkResumeState = dsFPD_LED_DEVICE_ACTIVE;
/* Thread-local flag set by the LED worker so the atexit handler can
 * detect a self-join scenario without reading shared state under a mutex.
 */
static _Thread_local bool gIsLEDWorkerThread = false;
#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
static int gProcessLockFd = -1;
#endif
#define DSFPD_LED_SYSFS_PATH_SIZE PATH_MAX
#define DSFPD_LED_FILE_PATH_SIZE PATH_MAX
#define DSFPD_TRIGGER_TOKEN_SIZE 256
#define DSFPD_TRIGGER_TEXT_BUFFER_SIZE 4096
#define DSFPD_BACKUP_TEXT_BUFFER_SIZE 4096
#define DSFPD_SMALL_TEXT_BUFFER_SIZE 64
#define DSFPD_NUMERIC_TEXT_BUFFER_SIZE 32

static char gLedSysfsPath[DSFPD_LED_SYSFS_PATH_SIZE] = {0};
static char gPreviousTrigger[DSFPD_TRIGGER_TOKEN_SIZE] = {0};
static unsigned int gLedMaxBrightness = 1;

#define SYSFS_LED_BRIGHTNESS_FILE "brightness"
#define SYSFS_LED_MAX_BRIGHTNESS_FILE "max_brightness"
#define SYSFS_LED_TRIGGER_FILE "trigger"
#define SYSFS_LED_TRIGGER_BACKUP_FILE "/run/lock/rpi_act_led_trigger.backup"
#define SYSFS_LED_PROCESS_LOCK_FILE "/run/lock/rpi_act_led_control.lock"

#define FPD_MUTEX_LOCK()   do { \
	pthread_mutex_lock(&gLEDStateMutex); \
} while(0)

#define FPD_MUTEX_UNLOCK()   do { \
	pthread_mutex_unlock(&gLEDStateMutex); \
} while(0)

/**
 * @brief Returns whether the FPD module is initialized.
 *
 * @return true when initialized, otherwise false.
 */
static bool isInitialized(void)
{
	bool initialized;
	FPD_MUTEX_LOCK();
	initialized = gIsFPDInitialized;
	FPD_MUTEX_UNLOCK();
	return initialized;
}

typedef struct {
	const unsigned int *durationsMs;
	size_t count;
	bool startOn;
} dsLedBlinkPattern_t;

static const unsigned int gPatternWpsConnecting[] = {500, 500};
static const unsigned int gPatternWpsConnected[] = {120, 120, 120, 640};
static const unsigned int gPatternWpsError[] = {80, 80};
static const unsigned int gPatternFactoryReset[] = {900, 250, 900, 250, 900, 1400};
static const unsigned int gPatternUsbUpgrade[] = {120, 120, 120, 120, 700, 1400};
static const unsigned int gPatternDownloadError[] = {
	120, 120, 120, 120, 120, 300,
	400, 120, 400, 120, 400, 300,
	120, 120, 120, 120, 120, 1400
};

/**
 * @brief Maps an LED state to its blink pattern definition.
 *
 * @param[in] state LED state to map.
 * @param[out] pattern Output pattern structure.
 *
 * @return true if a blink pattern exists for the state, otherwise false.
 */
static bool getPatternForState(dsFPDLedState_t state, dsLedBlinkPattern_t *pattern)
{
	if (pattern == NULL) {
		return false;
	}

	switch (state) {
		case dsFPD_LED_DEVICE_WPS_CONNECTING:
			pattern->durationsMs = gPatternWpsConnecting;
			pattern->count = sizeof(gPatternWpsConnecting) / sizeof(gPatternWpsConnecting[0]);
			pattern->startOn = true;
			return true;
		case dsFPD_LED_DEVICE_WPS_CONNECTED:
			pattern->durationsMs = gPatternWpsConnected;
			pattern->count = sizeof(gPatternWpsConnected) / sizeof(gPatternWpsConnected[0]);
			pattern->startOn = true;
			return true;
		case dsFPD_LED_DEVICE_WPS_ERROR:
			pattern->durationsMs = gPatternWpsError;
			pattern->count = sizeof(gPatternWpsError) / sizeof(gPatternWpsError[0]);
			pattern->startOn = true;
			return true;
		case dsFPD_LED_DEVICE_FACTORY_RESET:
			pattern->durationsMs = gPatternFactoryReset;
			pattern->count = sizeof(gPatternFactoryReset) / sizeof(gPatternFactoryReset[0]);
			pattern->startOn = true;
			return true;
		case dsFPD_LED_DEVICE_USB_UPGRADE:
			pattern->durationsMs = gPatternUsbUpgrade;
			pattern->count = sizeof(gPatternUsbUpgrade) / sizeof(gPatternUsbUpgrade[0]);
			pattern->startOn = true;
			return true;
		case dsFPD_LED_DEVICE_SOFTWARE_DOWNLOAD_ERROR:
			pattern->durationsMs = gPatternDownloadError;
			pattern->count = sizeof(gPatternDownloadError) / sizeof(gPatternDownloadError[0]);
			pattern->startOn = true;
			return true;
		default:
			return false;
	}
}

/**
 * @brief Adds milliseconds to an absolute timespec value.
 *
 * @param[in,out] ts Timespec to update.
 * @param[in] ms Milliseconds to add.
 */
static void addMsToTimespec(struct timespec *ts, unsigned int ms)
{
	ts->tv_sec += (time_t)(ms / 1000U);
	ts->tv_nsec += (long)((ms % 1000U) * 1000000L);
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec += 1;
		ts->tv_nsec -= 1000000000L;
	}
}

/**
 * @brief Reads a text file into a caller-provided buffer.
 *
 * @param[in] path File path.
 * @param[out] buffer Output buffer.
 * @param[in] bufferSize Size of output buffer.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t readTextFile(const char *path, char *buffer, size_t bufferSize)
{
	int fd;
	ssize_t bytesRead;
	size_t totalRead = 0;

	if (path == NULL || buffer == NULL || bufferSize < 2) {
		return dsERR_INVALID_PARAM;
	}

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		hal_err("Failed to open '%s' for read: %s\n", path, strerror(errno));
		return dsERR_GENERAL;
	}

	while (totalRead < bufferSize - 1) {
		bytesRead = read(fd, buffer + totalRead, (bufferSize - 1) - totalRead);
		if (bytesRead < 0) {
			if (errno == EINTR) {
				continue;
			}
			hal_err("Failed to read '%s': %s\n", path, strerror(errno));
			(void)close(fd);
			return dsERR_GENERAL;
		}
		if (bytesRead == 0) {
			break;
		}
		totalRead += (size_t)bytesRead;
	}
	(void)close(fd);

	buffer[totalRead] = '\0';
	return dsERR_NONE;
}

/**
 * @brief Writes a full string value to a text file.
 *
 * @param[in] path File path.
 * @param[in] value Null-terminated value to write.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t writeTextFile(const char *path, const char *value)
{
	int fd;
	size_t len;
	size_t totalWritten = 0;
	ssize_t bytesWritten;

	if (path == NULL || value == NULL) {
		return dsERR_INVALID_PARAM;
	}

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		hal_err("Failed to open '%s' for write: %s\n", path, strerror(errno));
		return dsERR_GENERAL;
	}

	len = strlen(value);
	while (totalWritten < len) {
		bytesWritten = write(fd, value + totalWritten, len - totalWritten);
		if (bytesWritten < 0) {
			if (errno == EINTR) {
				continue;
			}
			hal_err("Failed to write '%s': %s\n", path, strerror(errno));
			(void)close(fd);
			return dsERR_GENERAL;
		}
		if (bytesWritten == 0) {
			hal_err("Failed to write '%s': short write (0 bytes)\n", path);
			(void)close(fd);
			return dsERR_GENERAL;
		}
		totalWritten += (size_t)bytesWritten;
	}
	(void)close(fd);

	return dsERR_NONE;
}

/**
 * @brief Reads an unsigned integer from a text file.
 *
 * @param[in] path File path.
 * @param[out] value Parsed unsigned value.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t readUintFromFile(const char *path, unsigned int *value)
{
	char data[DSFPD_SMALL_TEXT_BUFFER_SIZE];
	char *endPtr = NULL;
	unsigned long parsed;

	if (value == NULL) {
		return dsERR_INVALID_PARAM;
	}

	if (readTextFile(path, data, sizeof(data)) != dsERR_NONE) {
		return dsERR_GENERAL;
	}

	errno = 0;
	parsed = strtoul(data, &endPtr, 10);
	if (errno == ERANGE || endPtr == data) {
		return dsERR_GENERAL;
	}

	/* Reject trailing non-whitespace characters (e.g. garbled sysfs output) */
	if (endPtr != NULL) {
		const char *p = endPtr;
		while (*p != '\0') {
			if (!isspace((unsigned char)*p)) {
				return dsERR_GENERAL;
			}
			++p;
		}
	}

	/* Guard against values that exceed unsigned int range */
	if (parsed > (unsigned long)UINT_MAX) {
		return dsERR_GENERAL;
	}

	*value = (unsigned int)parsed;
	return dsERR_NONE;
}

/**
 * @brief Builds a full sysfs LED file path under the detected LED directory.
 *
 * @param[out] outPath Destination path buffer.
 * @param[in] outPathSize Destination buffer size.
 * @param[in] fileName Sysfs file name.
 */
static void composeLedFilePath(char *outPath, size_t outPathSize, const char *fileName)
{
	size_t baseLen;
	size_t nameLen;
	size_t needed;

	if (outPath == NULL || outPathSize == 0 || fileName == NULL) {
		return;
	}

	outPath[0] = '\0';
	baseLen = strnlen(gLedSysfsPath, sizeof(gLedSysfsPath));
	if (baseLen == 0 || baseLen >= sizeof(gLedSysfsPath)) {
		return;
	}

	nameLen = strlen(fileName);
	if (baseLen > (SIZE_MAX - nameLen - 2U)) {
		return;
	}

	needed = baseLen + 1U + nameLen + 1U;
	if (needed > outPathSize) {
		return;
	}

	memcpy(outPath, gLedSysfsPath, baseLen);
	outPath[baseLen] = '/';
	memcpy(outPath + baseLen + 1U, fileName, nameLen);
	outPath[baseLen + 1U + nameLen] = '\0';
}

/**
 * @brief Detects the ACT LED sysfs directory on the current platform.
 *
 * @return dsERR_NONE when found, otherwise dsERR_OPERATION_NOT_SUPPORTED.
 */
static dsError_t detectLedPath(void)
{
	const char *candidates[] = {
		"/sys/class/leds/ACT",
		"/sys/class/leds/led0",
		"/sys/class/leds/act"
	};
	size_t i;

	for (i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
		if (access(candidates[i], F_OK) == 0) {
			snprintf(gLedSysfsPath, sizeof(gLedSysfsPath), "%s", candidates[i]);
			return dsERR_NONE;
		}
	}

	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Reads the currently active LED trigger token from sysfs.
 *
 * The sysfs trigger file usually contains multiple trigger names where the
 * active trigger is enclosed in brackets, e.g. "none [mmc0] timer". This
 * helper extracts the active token. If brackets are not found, it falls back
 * to the first whitespace-delimited token.
 *
 * @param[out] buffer Output trigger token.
 * @param[in] bufferSize Output buffer size.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t readCurrentSysfsTrigger(char *buffer, size_t bufferSize)
{
	char triggerPath[DSFPD_LED_FILE_PATH_SIZE];
	char triggerData[DSFPD_TRIGGER_TEXT_BUFFER_SIZE] = {0};
	char *tokenStart;
	char *tokenEnd;
	size_t tokenLen;

	if (buffer == NULL || bufferSize < 2) {
		return dsERR_INVALID_PARAM;
	}

	buffer[0] = '\0';
	composeLedFilePath(triggerPath, sizeof(triggerPath), SYSFS_LED_TRIGGER_FILE);
	if (readTextFile(triggerPath, triggerData, sizeof(triggerData)) != dsERR_NONE) {
		return dsERR_GENERAL;
	}

	tokenStart = strchr(triggerData, '[');
	if (tokenStart != NULL) {
		++tokenStart;
		tokenEnd = strchr(tokenStart, ']');
		if (tokenEnd == NULL || tokenEnd <= tokenStart) {
			return dsERR_GENERAL;
		}
	} else {
		/* Fallback when the active token is not bracketed. */
		tokenStart = triggerData;
		while (*tokenStart != '\0' && isspace((unsigned char)*tokenStart)) {
			++tokenStart;
		}
		if (*tokenStart == '\0') {
			return dsERR_GENERAL;
		}
		tokenEnd = tokenStart;
		while (*tokenEnd != '\0' && !isspace((unsigned char)*tokenEnd)) {
			++tokenEnd;
		}
	}

	tokenLen = (size_t)(tokenEnd - tokenStart);
	if (tokenLen == 0 || tokenLen >= bufferSize) {
		return dsERR_GENERAL;
	}

	memcpy(buffer, tokenStart, tokenLen);
	buffer[tokenLen] = '\0';
	return dsERR_NONE;
}

/**
 * @brief Caches the currently active LED trigger from sysfs.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t cacheCurrentTrigger(void)
{
	return readCurrentSysfsTrigger(gPreviousTrigger, sizeof(gPreviousTrigger));
}

/**
 * @brief Loads a previously saved LED trigger value from backup file.
 *
 * @param[out] trigger Output trigger string.
 * @param[in] triggerSize Output buffer size.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t loadTriggerBackup(char *trigger, size_t triggerSize)
{
	char data[DSFPD_BACKUP_TEXT_BUFFER_SIZE] = {0};
	size_t len;
	int fd;
	struct stat st;
	ssize_t n;
	size_t total = 0;

	if (trigger == NULL || triggerSize == 0) {
		return dsERR_INVALID_PARAM;
	}

	fd = open(SYSFS_LED_TRIGGER_BACKUP_FILE, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		return dsERR_GENERAL;
	}

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		(void)close(fd);
		return dsERR_GENERAL;
	}

	while (total < sizeof(data) - 1) {
		n = read(fd, data + total, (sizeof(data) - 1) - total);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(fd);
			return dsERR_GENERAL;
		}
		if (n == 0) {
			break;
		}
		total += (size_t)n;
	}
	(void)close(fd);

	if (total == 0) {
		return dsERR_GENERAL;
	}
	data[total] = '\0';

	len = strlen(data);
	while (len > 0 && (data[len - 1] == '\n' || data[len - 1] == '\r' || data[len - 1] == ' ' || data[len - 1] == '\t')) {
		data[len - 1] = '\0';
		len--;
	}

	if (len == 0) {
		return dsERR_GENERAL;
	}

	if (len >= triggerSize) {
		return dsERR_GENERAL;
	}

	memcpy(trigger, data, len);
	trigger[len] = '\0';
	return dsERR_NONE;
}

/**
 * @brief Persists the current trigger value for crash-safe restore.
 *
 * Writes to a temporary file in the same directory and renames it into
 * place atomically so the backup is always either the old or new value,
 * never partially written.
 *
 * @param[in] trigger Trigger string to persist.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t saveTriggerBackup(const char *trigger)
{
	char tmpPath[sizeof(SYSFS_LED_TRIGGER_BACKUP_FILE) + 4];
	int fd;
	struct stat st;
	size_t len;
	size_t total = 0;
	ssize_t n;

	if (trigger == NULL || trigger[0] == '\0') {
		return dsERR_INVALID_PARAM;
	}

	if (snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", SYSFS_LED_TRIGGER_BACKUP_FILE)
			>= (int)sizeof(tmpPath)) {
		return dsERR_GENERAL;
	}

	fd = open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		return dsERR_GENERAL;
	}

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		(void)close(fd);
		(void)unlink(tmpPath);
		return dsERR_GENERAL;
	}

	len = strlen(trigger);
	while (total < len) {
		n = write(fd, trigger + total, len - total);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(fd);
			(void)unlink(tmpPath);
			return dsERR_GENERAL;
		}
		if (n == 0) {
			(void)close(fd);
			(void)unlink(tmpPath);
			return dsERR_GENERAL;
		}
		total += (size_t)n;
	}

	if (fsync(fd) != 0) {
		(void)close(fd);
		(void)unlink(tmpPath);
		return dsERR_GENERAL;
	}

	if (close(fd) != 0) {
		(void)unlink(tmpPath);
		return dsERR_GENERAL;
	}

	if (rename(tmpPath, SYSFS_LED_TRIGGER_BACKUP_FILE) != 0) {
		(void)unlink(tmpPath);
		return dsERR_GENERAL;
	}

	/* Best-effort fsync of the parent directory to make the rename durable. */
	{
		char dirPath[PATH_MAX];
		char *dirName;
		int dirfd;
		if (strlen(SYSFS_LED_TRIGGER_BACKUP_FILE) < sizeof(dirPath)) {
			(void)strcpy(dirPath, SYSFS_LED_TRIGGER_BACKUP_FILE);
			dirName = dirname(dirPath);
			if (dirName != NULL) {
				dirfd = open(dirName, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
				if (dirfd >= 0) {
					(void)fsync(dirfd);
					(void)close(dirfd);
				}
			}
		}
	}

	return dsERR_NONE;
}

/**
 * @brief Removes the persisted trigger backup file.
 */
static void clearTriggerBackup(void)
{
	(void)unlink(SYSFS_LED_TRIGGER_BACKUP_FILE);
}

/**
 * @brief Sets the LED trigger mode in sysfs.
 *
 * @param[in] trigger Trigger name.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t setLedTrigger(const char *trigger)
{
	char triggerPath[DSFPD_LED_FILE_PATH_SIZE];

	composeLedFilePath(triggerPath, sizeof(triggerPath), SYSFS_LED_TRIGGER_FILE);
	return writeTextFile(triggerPath, trigger);
}

/**
 * @brief Writes raw LED brightness value to sysfs.
 *
 * @param[in] raw Raw brightness value in sysfs scale.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t writeLedBrightnessRaw(unsigned int raw)
{
	char brightnessPath[DSFPD_LED_FILE_PATH_SIZE];
	char value[DSFPD_NUMERIC_TEXT_BUFFER_SIZE];

	if (raw > gLedMaxBrightness) {
		raw = gLedMaxBrightness;
	}

	composeLedFilePath(brightnessPath, sizeof(brightnessPath), SYSFS_LED_BRIGHTNESS_FILE);
	snprintf(value, sizeof(value), "%u", raw);
	return writeTextFile(brightnessPath, value);
}

/**
 * @brief Converts percentage brightness to platform raw brightness.
 *
 * @param[in] percent Brightness in 0-100 range.
 *
 * @return Raw brightness value clamped to supported range.
 */
static unsigned int percentToRawBrightness(dsFPDBrightness_t percent)
{
	uint64_t tmp;
	unsigned int raw;

	if (percent == 0) {
		return 0;
	}

	tmp = (((uint64_t)percent * (uint64_t)gLedMaxBrightness) + 99ULL) / 100ULL;
	if (tmp == 0ULL) {
		tmp = 1ULL;
	}

	if (tmp > (uint64_t)gLedMaxBrightness) {
		tmp = (uint64_t)gLedMaxBrightness;
	}

	if (tmp > (uint64_t)UINT_MAX) {
		tmp = (uint64_t)UINT_MAX;
	}
	raw = (unsigned int)tmp;

	return raw;
}

/**
 * @brief Best-effort restore of LED trigger without state mutex.
 *
 * Used by atexit when mutex is busy so shutdown does not hang while still
 * attempting to restore kernel-controlled LED behavior.
 */
static void dsFPBestEffortRestoreOnExit(const char *preferredTrigger, const char *preferredLedPath)
{
	const char *triggerCandidates[] = {
		"/sys/class/leds/ACT/trigger",
		"/sys/class/leds/led0/trigger",
		"/sys/class/leds/act/trigger"
	};
	char restoreTrigger[sizeof(gPreviousTrigger)] = {0};
	char preferredTriggerPath[DSFPD_LED_FILE_PATH_SIZE];
	char triggerLine[sizeof(gPreviousTrigger) + 2];
	size_t triggerLineLen = 0;
	size_t i;

	if (preferredTrigger != NULL && preferredTrigger[0] != '\0') {
		snprintf(restoreTrigger, sizeof(restoreTrigger), "%s", preferredTrigger);
	} else if (loadTriggerBackup(restoreTrigger, sizeof(restoreTrigger)) != dsERR_NONE) {
		hal_info("Best-effort restore skipped trigger write: no persisted trigger available.\n");
		return;
	}

	{
		int lineLen = snprintf(triggerLine, sizeof(triggerLine), "%s\n", restoreTrigger);
		if (lineLen <= 0 || (size_t)lineLen >= sizeof(triggerLine)) {
			hal_err("Best-effort trigger restore skipped: trigger token is invalid.\n");
			return;
		}
		triggerLineLen = (size_t)lineLen;
	}

	if (preferredLedPath != NULL && preferredLedPath[0] != '\0') {
		if (snprintf(preferredTriggerPath, sizeof(preferredTriggerPath), "%s/%s",
					 preferredLedPath, SYSFS_LED_TRIGGER_FILE) < (int)sizeof(preferredTriggerPath)) {
			int fd = open(preferredTriggerPath, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
			if (fd >= 0) {
				ssize_t n = write(fd, triggerLine, triggerLineLen);
				if (n != (ssize_t)triggerLineLen) {
					hal_err("Best-effort trigger restore write failed on '%s': %s\n",
							preferredTriggerPath, (n < 0) ? strerror(errno) : "short write");
				}
				(void)close(fd);
				return;
			}
		}
	}

	for (i = 0; i < (sizeof(triggerCandidates) / sizeof(triggerCandidates[0])); ++i) {
		int fd = open(triggerCandidates[i], O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
		if (fd >= 0) {
			ssize_t n = write(fd, triggerLine, triggerLineLen);
			if (n != (ssize_t)triggerLineLen) {
				hal_err("Best-effort trigger restore write failed on '%s': %s\n",
						triggerCandidates[i], (n < 0) ? strerror(errno) : "short write");
			}
			(void)close(fd);
			break;
		}
	}
}

/**
 * @brief Updates current LED state and wakes pattern worker.
 *
 * @param[in] state New LED state.
 *
 * @return dsERR_NONE always.
 */
static dsError_t applyLedStateLocked(dsFPDLedState_t state)
{
	gCurrentLEDState = state;
	pthread_cond_signal(&gLEDPatternCond);
	return dsERR_NONE;
}

/**
 * @brief Returns whether the selected indicator is ON.
 *
 * Caller must hold gLEDStateMutex.
 */
static bool isFPStateEnabledLocked(dsFPDIndicator_t indicator)
{
	if (!dsFPDIndicator_isValid(indicator)) {
		return false;
	}

	return gFPState == dsFPD_STATE_ON;
}

/**
 * @brief Returns whether indicator is supported by this platform.
 *
 * Raspberry Pi implementation supports only POWER indicator APIs.
 */
static bool isIndicatorSupported(dsFPDIndicator_t indicator)
{
	return indicator == dsFPD_INDICATOR_POWER;
}

#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
/**
 * @brief Acquires exclusive inter-process ownership of LED control.
 *
 * @return dsERR_NONE on success, otherwise error code.
 */
static dsError_t acquireProcessLock(void)
{
	int fd;
	struct flock lock = {0};
	struct stat st;
	char pidText[DSFPD_NUMERIC_TEXT_BUFFER_SIZE];
	int len;

	if (gProcessLockFd >= 0) {
		return dsERR_NONE;
	}

	fd = open(SYSFS_LED_PROCESS_LOCK_FILE, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		int err = errno;
		hal_err("Unable to open process lock file '%s': errno=%d (%s)\n",
				SYSFS_LED_PROCESS_LOCK_FILE, err, strerror(err));
		return dsERR_GENERAL;
	}

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		hal_err("Process lock file is not a regular file: %s\n", SYSFS_LED_PROCESS_LOCK_FILE);
		(void)close(fd);
		return dsERR_GENERAL;
	}

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLK, &lock) != 0) {
		if (errno == EACCES || errno == EAGAIN) {
			hal_err("ACT LED control is already owned by another process.\n");
		} else {
			hal_err("Unable to acquire process lock: errno=%d\n", errno);
		}
		(void)close(fd);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	(void)ftruncate(fd, 0);
	len = snprintf(pidText, sizeof(pidText), "%ld\n", (long)getpid());
	if (len > 0) {
		size_t toWrite = (size_t)len;
		size_t written = 0;
		while (written < toWrite) {
			ssize_t n = write(fd, pidText + written, toWrite - written);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				break;
			}
			if (n == 0) {
				break;
			}
			written += (size_t)n;
		}
	}

	gProcessLockFd = fd;
	return dsERR_NONE;
}

/**
 * @brief Releases inter-process ownership lock of LED control.
 */
static void releaseProcessLock(void)
{
	if (gProcessLockFd >= 0) {
		struct flock lock = {0};
		lock.l_type = F_UNLCK;
		lock.l_whence = SEEK_SET;
		(void)fcntl(gProcessLockFd, F_SETLK, &lock);
		(void)close(gProcessLockFd);
		gProcessLockFd = -1;
	}
}
#endif

/**
 * @brief Best-effort process-exit cleanup for normal termination paths.
 *
 * Invoked via atexit() to stop the worker and restore trigger when process
 * exits without an explicit dsFPTerm() call.
 */
static void dsFPExitCleanup(void)
{
	bool initialized = false;
	bool shouldJoin = false;
	pthread_t threadToJoin;
	char exitTrigger[sizeof(gPreviousTrigger)] = {0};
	char exitLedPath[DSFPD_LED_SYSFS_PATH_SIZE] = {0};
	int lockRc;

	hal_info("invoked.\n");
	/* gIsLEDWorkerThread is set by ledPatternWorker() on entry with no
	 * mutex required, so it is safe to read here in the atexit handler.
	 */
	if (gIsLEDWorkerThread) {
		hal_info("Skipping dsFPTerm() in LED worker thread atexit handler to avoid self-join.\n");
		return;
	}

	/* Best-effort exit path: do not block on the state mutex during process
	 * shutdown, otherwise atexit can hang if another thread holds the lock.
	 */
	lockRc = pthread_mutex_trylock(&gLEDStateMutex);
	if (lockRc != 0) {
		hal_info("LED state mutex is busy during atexit; attempting best-effort trigger restore without mutex.\n");
		dsFPBestEffortRestoreOnExit(NULL, NULL);
		return;
	}

	/* We hold the state mutex without blocking: perform minimal shutdown
	 * state transitions here so there is no unlock/relock race window.
	 */
	initialized = gIsFPDInitialized;
	if (initialized) {
		if (gPreviousTrigger[0] != '\0') {
			snprintf(exitTrigger, sizeof(exitTrigger), "%s", gPreviousTrigger);
		}
		if (gLedSysfsPath[0] != '\0') {
			snprintf(exitLedPath, sizeof(exitLedPath), "%s", gLedSysfsPath);
		}

		gLEDPatternThreadStop = true;
		pthread_cond_signal(&gLEDPatternCond);
		if (gLEDPatternThreadRunning) {
			threadToJoin = gLEDPatternThread;
			gLEDPatternThreadRunning = false;
			shouldJoin = true;
		}
		gIsFPDInitialized = false;
	}
	(void)pthread_mutex_unlock(&gLEDStateMutex);

	if (!initialized) {
		return;
	}

	if (shouldJoin) {
		(void)pthread_join(threadToJoin, NULL);
	}

	/* During process exit, restore LED control without taking the state mutex. */
	dsFPBestEffortRestoreOnExit(exitTrigger, exitLedPath);

#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
	releaseProcessLock();
#endif
}

/**
 * @brief Worker thread that drives steady and blinking LED behavior.
 *
 * @param[in] arg Unused thread argument.
 *
 * @return NULL on thread exit.
 */
static void *ledPatternWorker(void *arg)
{
	hal_info("LED pattern worker thread started.\n");
	/* Mark this thread so the atexit handler can skip dsFPTerm() if
	 * this thread itself calls exit(), avoiding a self-join deadlock.
	 */
	gIsLEDWorkerThread = true;
	dsFPDLedState_t activeState = dsFPD_LED_DEVICE_NONE;
	dsFPDBrightness_t activeBrightness = dsFPD_BRIGHTNESS_MAX;
	size_t phaseIndex = 0;
	uint64_t activeCustomReqId = 0;
	bool customPhaseOn = true;
	unsigned int customPhasesRemaining = 0;

	(void)arg;

	while (true) {
		dsLedBlinkPattern_t pattern;
		dsFPDLedState_t state;
		dsFPDBrightness_t brightness;
		unsigned int rawOn;
		bool isBlink;
		bool useCustomBlink = false;
		bool customLedOn = false;
		unsigned int customDurationMs = 0;

		FPD_MUTEX_LOCK();
		while (!gLEDPatternThreadStop && !gIsFPDInitialized) {
			pthread_cond_wait(&gLEDPatternCond, &gLEDStateMutex);
		}

		if (gLEDPatternThreadStop) {
			FPD_MUTEX_UNLOCK();
			break;
		}

		state = gCurrentLEDState;
		brightness = gCurrentBrightness;
		rawOn = percentToRawBrightness(brightness);
		isBlink = getPatternForState(state, &pattern);

		if (gCustomBlinkActive) {
			useCustomBlink = true;
			if (activeCustomReqId != gCustomBlinkRequestId) {
				activeCustomReqId = gCustomBlinkRequestId;
				customPhaseOn = true;
				customPhasesRemaining = gCustomBlinkIterations * 2U;
			}

			if (customPhasesRemaining == 0U) {
				gCustomBlinkActive = false;
				useCustomBlink = false;
				activeCustomReqId = 0;
				customPhaseOn = true;
				if (applyLedStateLocked(gCustomBlinkResumeState) != dsERR_NONE) {
					hal_err("Failed to restore LED state after custom blink sequence.\n");
				}
				state = gCurrentLEDState;
				brightness = gCurrentBrightness;
				rawOn = percentToRawBrightness(brightness);
				isBlink = getPatternForState(state, &pattern);
			} else {
				customLedOn = customPhaseOn;
				customDurationMs = gCustomBlinkDurationMs;
			}
		}

		if (state != activeState || brightness != activeBrightness) {
			phaseIndex = 0;
			activeState = state;
			activeBrightness = brightness;
		}

		if (!useCustomBlink && !isBlink) {
			unsigned int raw = (state == dsFPD_LED_DEVICE_STANDBY || state == dsFPD_LED_DEVICE_NONE) ? 0 : rawOn;
			FPD_MUTEX_UNLOCK();
			if (dsERR_NONE != writeLedBrightnessRaw(raw)) {
				hal_err("Failed to apply LED brightness for state=%d in steady mode.\n", activeState);
			}

			FPD_MUTEX_LOCK();
			while (!gLEDPatternThreadStop) {
				/* Re-read state under the lock before waiting to catch any
				 * signal that fired while the mutex was unlocked for I/O.
				 */
				state = gCurrentLEDState;
				brightness = gCurrentBrightness;
				if (state != activeState || brightness != activeBrightness || gCustomBlinkActive) {
					break;
				}
				pthread_cond_wait(&gLEDPatternCond, &gLEDStateMutex);
			}
			FPD_MUTEX_UNLOCK();
			continue;
		}

		{
			bool ledOn = useCustomBlink ? customLedOn : (((phaseIndex % 2U) == 0U) ? pattern.startOn : !pattern.startOn);
			unsigned int raw = ledOn ? rawOn : 0;
			unsigned int durationMs = useCustomBlink ? customDurationMs : pattern.durationsMs[phaseIndex % pattern.count];
			struct timespec wakeTime;

			FPD_MUTEX_UNLOCK();
			if (dsERR_NONE != writeLedBrightnessRaw(raw)) {
				hal_err("Failed to apply LED brightness for state=%d in blink mode.\n", activeState);
			}

			clock_gettime(CLOCK_MONOTONIC, &wakeTime);
			addMsToTimespec(&wakeTime, durationMs);

			FPD_MUTEX_LOCK();
			if (!gLEDPatternThreadStop) {
				/* Re-check under lock before timed wait so a state/brightness
				 * update that happened while unlocked is handled immediately.
				 */
				state = gCurrentLEDState;
				brightness = gCurrentBrightness;
				if (state != activeState || brightness != activeBrightness) {
					phaseIndex = 0;
					activeState = state;
					activeBrightness = brightness;
				} else {
					int waitRc = pthread_cond_timedwait(&gLEDPatternCond, &gLEDStateMutex, &wakeTime);
					if (waitRc == ETIMEDOUT) {
						if (useCustomBlink) {
							if (customPhasesRemaining > 0U) {
								customPhasesRemaining--;
								customPhaseOn = !customPhaseOn;
							}
							if (customPhasesRemaining == 0U) {
								gCustomBlinkActive = false;
								if (applyLedStateLocked(gCustomBlinkResumeState) != dsERR_NONE) {
									hal_err("Failed to restore LED state after custom blink timeout.\n");
								}
							}
						} else {
							phaseIndex = (phaseIndex + 1U) % pattern.count;
						}
					} else {
						/* Non-timeout wake: check if state or brightness actually changed.
						 * If not, treat as spurious wakeup and advance phase normally.
						 */
						state = gCurrentLEDState;
						brightness = gCurrentBrightness;
						if (state != activeState || brightness != activeBrightness) {
							phaseIndex = 0;
							activeState = state;
							activeBrightness = brightness;
						} else if (!useCustomBlink) {
							phaseIndex = (phaseIndex + 1U) % pattern.count;
						}
					}
				}
			}
			FPD_MUTEX_UNLOCK();
		}
	}

	hal_info("LED pattern worker thread exiting.\n");

	return NULL;
}

/**
 * @brief Initializes the Front Panel Display (FPD) sub-module of Device Settings HAL
 *
 * This function allocates required resources for Front Panel and is required to be called before the other APIs in this module.
 *
 *
 * @return dsError_t                  -  Status
 * @retval dsERR_NONE                 -  Success
 * @retval dsERR_ALREADY_INITIALIZED  -  Function is already initialized
 * @retval dsERR_GENERAL              -  Underlying undefined platform error
 *
 * @post dsFPTerm() must be called to release resources
 *
 * @warning  This API is Not thread safe
 *
 * @see dsFPTerm()
 *
 */
dsError_t dsFPInit(void)
{
	hal_info("invoked.\n");

	FPD_MUTEX_LOCK();
	if (!gExitCleanupRegistered) {
		if (atexit(dsFPExitCleanup) == 0) {
			gExitCleanupRegistered = true;
		} else {
			hal_err("Unable to register process-exit cleanup handler.\n");
		}
	}

	if (gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Already initialized.\n");
		return dsERR_ALREADY_INITIALIZED;
	}

	if (!gCondVarReady) {
		pthread_condattr_t condattr;
		int rc = pthread_condattr_init(&condattr);
		if (rc != 0) {
			FPD_MUTEX_UNLOCK();
			hal_err("pthread_condattr_init failed: %s\n", strerror(rc));
			return dsERR_GENERAL;
		}
		rc = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
		if (rc != 0) {
			(void)pthread_condattr_destroy(&condattr);
			FPD_MUTEX_UNLOCK();
			hal_err("pthread_condattr_setclock(CLOCK_MONOTONIC) failed: %s\n", strerror(rc));
			return dsERR_GENERAL;
		}
		rc = pthread_cond_init(&gLEDPatternCond, &condattr);
		(void)pthread_condattr_destroy(&condattr);
		if (rc != 0) {
			FPD_MUTEX_UNLOCK();
			hal_err("pthread_cond_init failed: %s\n", strerror(rc));
			return dsERR_GENERAL;
		}
		gCondVarReady = true;
	}

	if (detectLedPath() != dsERR_NONE) {
		FPD_MUTEX_UNLOCK();
		hal_err("Unable to find ACT LED sysfs path.\n");
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
	{
		dsError_t lockErr = acquireProcessLock();
		if (lockErr != dsERR_NONE) {
			FPD_MUTEX_UNLOCK();
			return lockErr;
		}
	}
#endif

	if (loadTriggerBackup(gPreviousTrigger, sizeof(gPreviousTrigger)) == dsERR_NONE) {
		char currentTrigger[sizeof(gPreviousTrigger)] = {0};
		hal_info("Loaded previous trigger from backup: %s\n", gPreviousTrigger);
		if (readCurrentSysfsTrigger(currentTrigger, sizeof(currentTrigger)) == dsERR_NONE) {
			if (strcmp(currentTrigger, "none") != 0) {
				hal_info("Ignoring stale trigger backup '%s'; using current trigger '%s'.\n",
						 gPreviousTrigger, currentTrigger);
				snprintf(gPreviousTrigger, sizeof(gPreviousTrigger), "%s", currentTrigger);
				if (saveTriggerBackup(gPreviousTrigger) != dsERR_NONE) {
					hal_err("Unable to update trigger backup file with current trigger.\n");
				}
			} else {
				hal_info("Current trigger is 'none'; keeping backup trigger '%s'.\n", gPreviousTrigger);
			}
		} else {
			hal_err("Failed to read current sysfs trigger; proceeding with backup trigger '%s'.\n",
					gPreviousTrigger);
		}
	} else if (cacheCurrentTrigger() != dsERR_NONE) {
#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
		releaseProcessLock();
#endif
		FPD_MUTEX_UNLOCK();
		hal_err("Unable to determine current LED trigger; aborting initialization.\n");
		return dsERR_GENERAL;
	} else {
		hal_info("Current LED trigger before init: %s\n", gPreviousTrigger);
		if (saveTriggerBackup(gPreviousTrigger) != dsERR_NONE) {
#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
			releaseProcessLock();
#endif
			FPD_MUTEX_UNLOCK();
			hal_err("Unable to persist trigger backup file; aborting initialization.\n");
			return dsERR_GENERAL;
		}
	}

	if (setLedTrigger("none") != dsERR_NONE) {
#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
		releaseProcessLock();
#endif
		FPD_MUTEX_UNLOCK();
		hal_err("Unable to set LED trigger to none.\n");
		return dsERR_GENERAL;
	}

	{
		char maxBrightnessPath[DSFPD_LED_FILE_PATH_SIZE];
		unsigned int maxBrightness = 1;
		composeLedFilePath(maxBrightnessPath, sizeof(maxBrightnessPath), SYSFS_LED_MAX_BRIGHTNESS_FILE);
		if (readUintFromFile(maxBrightnessPath, &maxBrightness) != dsERR_NONE || maxBrightness == 0) {
			maxBrightness = 1;
		}
		gLedMaxBrightness = maxBrightness;
	}

	gCurrentBrightness = dsFPD_BRIGHTNESS_MAX;
	gCurrentLEDState = dsFPD_LED_DEVICE_ACTIVE;
	gFPState = dsFPD_STATE_ON;
	gCustomBlinkActive = false;
	gCustomBlinkDurationMs = 0;
	gCustomBlinkIterations = 0;
	gCustomBlinkRequestId = 0;
	gCustomBlinkResumeState = dsFPD_LED_DEVICE_ACTIVE;
	gLEDPatternThreadStop = false;
	if (pthread_create(&gLEDPatternThread, NULL, ledPatternWorker, NULL) != 0) {
		(void)writeLedBrightnessRaw(0);
		if (gPreviousTrigger[0] != '\0') {
			if (setLedTrigger(gPreviousTrigger) == dsERR_NONE) {
				clearTriggerBackup();
			} else {
				hal_err("Unable to restore LED trigger '%s' after worker thread create failure.\n", gPreviousTrigger);
			}
		}
		gLedSysfsPath[0] = '\0';
		gPreviousTrigger[0] = '\0';
#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
		releaseProcessLock();
#endif
		FPD_MUTEX_UNLOCK();
		hal_err("Unable to create LED pattern worker thread.\n");
		return dsERR_GENERAL;
	}
	gLEDPatternThreadRunning = true;

	gIsFPDInitialized = true;
	pthread_cond_signal(&gLEDPatternCond);
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @brief Sets blink pattern of specified Front Panel Display LED
 *
 * This function is used to set the individual discrete LED to blink for a specified number of iterations with blink interval.
 * This function must return dsERR_OPERATION_NOT_SUPPORTED if FP State is "OFF".
 * To stop the blink, either dsFPSetLEDState() or dsFPTerm() can be invoked.
 *
 * @param[in] eIndicator        -  FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] uBlinkDuration    -  Blink interval. The time in ms the text display will remain ON
 *                                   during one blink iteration.
 * @param[in] uBlinkIterations  -  The number of iterations per minute data will blink
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 *
 * @pre dsFPInit() and dsSetFPState() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsSetFPBlink(dsFPDIndicator_t eIndicator, unsigned int uBlinkDuration, unsigned int uBlinkIterations)
{
	hal_info("invoked.\n");

	if (!dsFPDIndicator_isValid(eIndicator) || uBlinkDuration == 0 || uBlinkIterations == 0) {
		hal_err("Invalid parameter, eIndicator: %d, uBlinkDuration: %u, uBlinkIterations: %u.\n",
				eIndicator, uBlinkDuration, uBlinkIterations);
		return dsERR_INVALID_PARAM;
	}

	if (!isIndicatorSupported(eIndicator)) {
		hal_err("Blink rejected: unsupported indicator eIndicator=%d.\n", eIndicator);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!isFPStateEnabledLocked(eIndicator)) {
		hal_err("FPState is %d.\n", gFPState);
		FPD_MUTEX_UNLOCK();
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	hal_info("Blink accepted: eIndicator=%d, durationMs=%u, iterations=%u, resumeLedState=%d.\n",
			eIndicator, uBlinkDuration, uBlinkIterations, gCurrentLEDState);

	gCustomBlinkDurationMs = uBlinkDuration;
	gCustomBlinkIterations = uBlinkIterations;
	gCustomBlinkResumeState = gCurrentLEDState;
	gCustomBlinkRequestId++;
	gCustomBlinkActive = true;
	/* Wake the worker so custom blink starts immediately. */
	pthread_cond_signal(&gLEDPatternCond);
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @brief Sets the brightness level of specified Front Panel Display LED
 *
 * This function will set the brightness of the specified discrete LED on the Front
 * Panel Display to the specified brightness level. This function must return dsERR_OPERATION_NOT_SUPPORTED
 * if the FP State is "OFF". HAL will neither retain the brightness value nor set any default brightness value.
 *
 * @param[in] eIndicator  - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] eBrightness - The brightness value(0 to 100) for the specified indicator.
 *                            Please refer ::dsFPDBrightness_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() and dsSetFPState() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPBrightness()
 *
 */
dsError_t dsSetFPBrightness(dsFPDIndicator_t eIndicator, dsFPDBrightness_t eBrightness)
{
	hal_info("invoked.\n");

	if (!dsFPDIndicator_isValid(eIndicator) || eBrightness > dsFPD_BRIGHTNESS_MAX) {
		hal_err("Invalid parameter, eIndicator: %d, eBrightness: %u.\n", eIndicator, eBrightness);
		return dsERR_INVALID_PARAM;
	}

	if (!isIndicatorSupported(eIndicator)) {
		hal_err("Unsupported indicator, eIndicator: %d.\n", eIndicator);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!isFPStateEnabledLocked(eIndicator)) {
		FPD_MUTEX_UNLOCK();
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	gCurrentBrightness = eBrightness;
	/* Wake worker so brightness is applied immediately. */
	pthread_cond_signal(&gLEDPatternCond);
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @brief Gets the brightness level of specified Front Panel Display LED
 *
 * This function returns the brightness level of the specified discrete LED on the Front
 * Panel. This function must return dsERR_OPERATION_NOT_SUPPORTED if FP State is "OFF".
 *
 * @param[in]  eIndicator  - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[out] pBrightness - current brightness value(0 to 100) of the specified indicator
 *                             Please refer ::dsFPDBrightness_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() and dsSetFPState() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetFPBrightness()
 *
 */
dsError_t dsGetFPBrightness(dsFPDIndicator_t eIndicator, dsFPDBrightness_t *pBrightness)
{
	hal_info("invoked.\n");

	if (!dsFPDIndicator_isValid(eIndicator) || pBrightness == NULL) {
		hal_err("Invalid parameter, eIndicator: %d, pBrightness: %p.\n", eIndicator, pBrightness);
		return dsERR_INVALID_PARAM;
	}

	if (!isIndicatorSupported(eIndicator)) {
		hal_err("Unsupported indicator, eIndicator: %d.\n", eIndicator);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!isFPStateEnabledLocked(eIndicator)) {
		FPD_MUTEX_UNLOCK();
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	*pBrightness = gCurrentBrightness;
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @brief Sets the indicator state of specified discrete Front Panel Display LED
 *
 *
 * @param[in] eIndicator - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] state      - Indicates the state of the indicator to be set. Please refer ::dsFPDState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPState()
 *
 */
dsError_t dsSetFPState(dsFPDIndicator_t eIndicator, dsFPDState_t state)
{
	hal_info("invoked.\n");

	if (!dsFPDIndicator_isValid(eIndicator) || state < dsFPD_STATE_OFF || state >= dsFPD_STATE_MAX) {
		hal_err("Invalid parameter, eIndicator: %d, state: %d.\n", eIndicator, state);
		return dsERR_INVALID_PARAM;
	}

	if (!isIndicatorSupported(eIndicator)) {
		hal_err("SetFPState rejected: unsupported indicator eIndicator=%d.\n", eIndicator);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	gFPState = state;
	hal_info("SetFPState applied: eIndicator=%d state=%d.\n", eIndicator, state);
	if (state == dsFPD_STATE_OFF) {
		/* Turning an indicator OFF should stop any in-progress custom blink. */
		gCustomBlinkActive = false;
	}

	pthread_cond_signal(&gLEDPatternCond);
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @brief Gets the indicator state of specified discrete Front Panel Display LED
 *
 *
 * @param[in]  eIndicator - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[out] state      - current state of the specified indicator. Please refer ::dsFPDState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPState()
 *
 */
dsError_t dsGetFPState(dsFPDIndicator_t eIndicator, dsFPDState_t* state)
{
	hal_info("invoked.\n");

	if (!dsFPDIndicator_isValid(eIndicator) || state == NULL) {
		hal_err("Invalid parameter, eIndicator: %d, state: %p.\n", eIndicator, state);
		return dsERR_INVALID_PARAM;
	}

	if (!isIndicatorSupported(eIndicator)) {
		hal_err("Unsupported indicator, eIndicator: %d.\n", eIndicator);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	*state = gFPState;
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @brief Sets the color of specified Front Panel Display LED
 *
 * This function sets the color of the specified Front Panel Indicator LED, if the
 * indicator supports it (i.e. is multi-colored). It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the indicator is single-colored or if the FP State is "OFF".
 *
 * @param[in] eIndicator    - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] eColor        - The color index for the specified indicator. Please refer ::dsFPDColor_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() and dsSetFPState() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPColor()
 *
 */
dsError_t dsSetFPColor(dsFPDIndicator_t eIndicator, dsFPDColor_t eColor)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!dsFPDIndicator_isValid(eIndicator) || eColor >= dsFPD_COLOR_MAX) {
		hal_err("Invalid parameter, eIndicator: %d, eColor: %d.\n", eIndicator, eColor);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for multi-color.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief  Gets the color of specified Front Panel Display LED
 *
 * This function gets the color of the specified Front Panel Indicator LED, if the
 * indicator supports it (i.e. is multi-colored). It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the indicator is single-colored or if the FP State is "OFF".
 *
 * @param[in] eIndicator - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[out] pColor    - current color value of the specified indicator. Please refer ::dsFPDColor_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() and dsSetFPState() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPColor()
 *
 */
dsError_t dsGetFPColor(dsFPDIndicator_t eIndicator, dsFPDColor_t *pColor)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!dsFPDIndicator_isValid(eIndicator) || pColor == NULL) {
		hal_err("Invalid parameter, eIndicator: %d, pColor: %p.\n", eIndicator, pColor);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for multi-color.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Sets the time on 7-Segment Front Panel Display LEDs
 *
 * This function sets the 7-segment display LEDs to show the time in specified format.
 * The format (12/24-hour) has to be specified. If there are no 7-Segment display LEDs present on the
 * device or if the FP State is "OFF" then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * It must return dsERR_INVALID_PARAM if the format and hours values do not agree,
 * or if the hours/minutes are invalid.
 * The FP Display Mode must be dsFPD_MODE_CLOCK/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eTimeFormat   - Time format (12 or 24 hrs). Please refer ::dsFPDTimeFormat_t
 * @param[in] uHour         - Hour information
 * @param[in] uMinutes      - Minutes information
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPTimeFormat()
 *
 */
dsError_t dsSetFPTime(dsFPDTimeFormat_t eTimeFormat, const unsigned int uHour, const unsigned int uMinutes)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (eTimeFormat < dsFPD_TIME_12_HOUR || eTimeFormat >= dsFPD_TIME_MAX || uMinutes >= 60
			|| (eTimeFormat == dsFPD_TIME_12_HOUR ? (uHour < 1 || uHour > 12) : (uHour > 23))) {
		hal_err("Invalid parameter, eTimeFormat: %d, uHour: %u, uMinutes: %u.\n",
				eTimeFormat, uHour, uMinutes);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for setting time.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Displays the specified text on 7-segment Front Panel Display LEDs
 *
 * This function is used to set the 7-segment display LEDs to show the given text.
 * If there are no 7-Segment display LEDs present on the device or if the FP State is "OFF",
 * then dsERR_OPERATION_NOT_SUPPORTED must be returned. Please refer ::dsFPDState_t.
 * The FP Display Mode must be dsFPD_MODE_TEXT/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @param[in] pText - Text to be displayed. Maximum length of Text is 10 characters.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTextBrightness()
 *
 */
dsError_t dsSetFPText(const char* pText)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (pText == NULL || strlen(pText) > (size_t)dsFPD_TEXTDISP_MAX) {
		hal_err("Invalid parameter, pText: %p or length %zu.\n",
				pText, (pText != NULL) ? strlen(pText) : (size_t)0);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for setting text.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Sets the brightness level of 7-segment Front Panel Display LEDs
 *
 * This function will set the brightness of the specified 7-segment display LEDs on the Front
 * Panel Display to the specified brightness level. If there are no 7-Segment display LEDs present
 * on the device or if the FP State is "OFF" then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * The FP Display Mode must be dsFPD_MODE_TEXT/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eIndicator    - FPD Text indicator index. Please refer ::dsFPDTextDisplay_t
 * @param[in] eBrightness   - The brightness value for the specified indicator. Valid range is from 0 to 100
 *                              Please refer ::dsFPDBrightness_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPTextBrightness(), dsSetFPText()
 *
 */
dsError_t dsSetFPTextBrightness(dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t eBrightness)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!dsFPDTextDisplay_isValid(eIndicator) || eBrightness > dsFPD_BRIGHTNESS_MAX) {
		hal_err("Invalid parameter, eIndicator: %d, eBrightness: %u.\n", eIndicator, eBrightness);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for setting text brightness.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Gets the brightness of 7-segment Front Panel Display LEDs
 *
 * This function will get the brightness of the specified 7-segment display LEDs on the Front
 * Panel Text Display. If there are no 7-segment display LEDs present or if the FP State is "OFF"
 * then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * The FP Display Mode must be dsFPD_MODE_CLOCK/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eIndicator    - FPD Text indicator index. Please refer ::dsFPDTextDisplay_t
 * @param[out] eBrightness  - Brightness value. Valid range is from 0 to 100. Please refer ::dsFPDBrightness_t.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTextBrightness()
 *
 */
dsError_t dsGetFPTextBrightness(dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t *eBrightness)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (!dsFPDTextDisplay_isValid(eIndicator) || eBrightness == NULL) {
		hal_err("Invalid parameter, eIndicator: %d, eBrightness: %p.\n", eIndicator, eBrightness);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for getting text brightness.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Enables/Disables the clock display of Front Panel Display LEDs
 *
 * This function will enable or disable displaying of clock. It will return dsERR_OPERATION_NOT_SUPPORTED
 * if Clock display is not available
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template_template file.
 *
 * @param[in] enable    - Indicates the clock to be enabled or disabled.
 *                          1 if enabled, 0 if disabled.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t / If Clock display is not available
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsFPEnableCLockDisplay(int enable)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (enable != 0 && enable != 1) {
		hal_err("Invalid parameter, enable: %d.\n", enable);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for clock display.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Enables Text Scrolling on 7-segment Front Panel Display LEDs
 *
 * This function scrolls the text in the 7-segment display LEDs for the given number of iterations.
 * If there are no 7-segment display LEDs present or if the FP State is "OFF" then
 * dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * Horizontal and Vertical scroll cannot work at the same time. If both values are non-zero values
 * it should return dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] uScrollHoldOnDur       - Duration in ms to hold each char before scrolling to the next position
 *                                       during one scroll iteration
 * @param[in] uHorzScrollIterations  - Number of iterations to scroll horizontally
 * @param[in] uVertScrollIterations  - Number of iterations to scroll vertically
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsSetFPScroll(unsigned int uScrollHoldOnDur, unsigned int uHorzScrollIterations, unsigned int uVertScrollIterations)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (uScrollHoldOnDur == 0 || (uHorzScrollIterations == 0 && uVertScrollIterations == 0)) {
		hal_err("Invalid parameter, uScrollHoldOnDur: %u, uHorzScrollIterations: %u, uVertScrollIterations: %u.\n",
				uScrollHoldOnDur, uHorzScrollIterations, uVertScrollIterations);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for text scrolling.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Terminates the Front Panel Display sub-module
 *
 * This function resets any data structures used within Front Panel sub-module,
 * and releases all the resources allocated during the init function.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning This API is Not thread safe
 *
 * @see dsFPInit()
 *
 */
dsError_t dsFPTerm(void)
{
	hal_info("invoked.\n");
	pthread_t threadToJoin;
	bool shouldJoin = false;

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	gLEDPatternThreadStop = true;
	pthread_cond_signal(&gLEDPatternCond);
	if (gLEDPatternThreadRunning) {
		threadToJoin = gLEDPatternThread;
		gLEDPatternThreadRunning = false;
		shouldJoin = true;
	}
	gIsFPDInitialized = false;
	FPD_MUTEX_UNLOCK();

	if (shouldJoin) {
		(void)pthread_join(threadToJoin, NULL);
	}

	FPD_MUTEX_LOCK();

	(void)writeLedBrightnessRaw(0);
	if (gPreviousTrigger[0] == '\0') {
		(void)loadTriggerBackup(gPreviousTrigger, sizeof(gPreviousTrigger));
	}
	if (gPreviousTrigger[0] != '\0') {
		if (setLedTrigger(gPreviousTrigger) != dsERR_NONE) {
			hal_err("Unable to restore previous LED trigger '%s'.\n", gPreviousTrigger);
		} else {
			hal_info("LED trigger restored to pre-init value: %s\n", gPreviousTrigger);
			clearTriggerBackup();
		}
	}

	gCurrentLEDState = dsFPD_LED_DEVICE_NONE;
	gCurrentBrightness = dsFPD_BRIGHTNESS_MAX;
	gFPState = dsFPD_STATE_OFF;
	gCustomBlinkActive = false;
	gCustomBlinkDurationMs = 0;
	gCustomBlinkIterations = 0;
	gCustomBlinkRequestId = 0;
	gCustomBlinkResumeState = dsFPD_LED_DEVICE_ACTIVE;
	gLedMaxBrightness = 1;
	gLedSysfsPath[0] = '\0';
	gPreviousTrigger[0] = '\0';
	gLEDPatternThreadStop = false;

#ifdef DSFPD_ENABLE_MULTI_PROCESS_GUARD
	releaseProcessLock();
#endif
	FPD_MUTEX_UNLOCK();

	return dsERR_NONE;
}

/**
 * @note This API is deprecated.
 *
 * @brief Sets the current time format on the 7-segment Front Panel Display LEDs
 *
 * This function sets the 7-segment display LEDs to show the
 * specified time in specified format. It must return dsERR_OPERATION_NOT_SUPPORTED
 * if the underlying hardware does not have support for clock.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eTimeFormat   -  Indicates the time format (12 hour or 24 hour).
 *                               Please refer ::dsFPDTimeFormat_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @note The time display should also change according to the new format set
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPTimeFormat()
 *
 */
dsError_t dsSetFPTimeFormat(dsFPDTimeFormat_t eTimeFormat)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (eTimeFormat < dsFPD_TIME_12_HOUR || eTimeFormat >= dsFPD_TIME_MAX) {
		hal_err("Invalid parameter, eTimeFormat: %d.\n", eTimeFormat);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for setting time format.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

 /**
 * @note This API is deprecated.
 *
 * @brief Gets the current time format on the 7-segment Front Panel Display LEDs
 *
 * This function gets the current time format set on 7-segment display LEDs panel.
 * It must return dsERR_OPERATION_NOT_SUPPORTED if the underlying hardware does not
 * have support for clock.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[out] pTimeFormat      - Current time format value (12 hour or 24 hour).
 *                                  Please refer ::dsFPDTimeFormat_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTimeFormat()
 *
 */
dsError_t dsGetFPTimeFormat(dsFPDTimeFormat_t *pTimeFormat)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (pTimeFormat == NULL) {
		hal_err("Invalid parameter, pTimeFormat: %p.\n", pTimeFormat);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for getting time format.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Sets the display mode of the Front Panel Display LEDs
 *
 * This function sets the display mode (clock or text or both) for FPD.
 * It must return dsERR_OPERATION_NOT_SUPPORTED if the underlying hardware does not
 * have support for Text or Clock.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eMode - Indicates the mode. Please refer ::dsFPDMode_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsSetFPDMode(dsFPDMode_t eMode)
{
	hal_info("invoked.\n");

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (eMode < dsFPD_MODE_ANY || eMode >= dsFPD_MODE_MAX) {
		hal_err("Invalid parameter, eMode: %d.\n", eMode);
		return dsERR_INVALID_PARAM;
	}

	hal_err("No Hardware support for setting display mode.\n");
	return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the current power Front Panel Display LED state
 *
 * This function gets the current power LED state
 *
 * @param[out] state - Current LED state. Please refer ::dsFPDLedState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsFPSetLEDState()
 */
dsError_t dsFPGetLEDState(dsFPDLedState_t* state)
{
	hal_info("invoked.\n");

	if (state == NULL) {
		hal_err("Invalid parameter, state: %p.\n", state);
		return dsERR_INVALID_PARAM;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (gCurrentLEDState <= dsFPD_LED_DEVICE_NONE || gCurrentLEDState >= dsFPD_LED_DEVICE_MAX) {
		FPD_MUTEX_UNLOCK();
		hal_err("Current LED state: %d is out of valid range.\n", gCurrentLEDState);
		return dsERR_GENERAL;
	}

	if (((1u << gCurrentLEDState) & gSupportedLEDStates) == 0u) {
		FPD_MUTEX_UNLOCK();
		hal_err("Current LED state: %d is unsupported.\n", gCurrentLEDState);
		return dsERR_GENERAL;
	}

	*state = gCurrentLEDState;
	FPD_MUTEX_UNLOCK();
	return dsERR_NONE;
}

/**
 * @brief Sets the current power Front Panel Display LED state
 *
 * This function sets the current power LED state
 *
 * @param[in] state - LED state. Please refer ::dsFPDLedState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsFPGetLEDState()
 */
dsError_t dsFPSetLEDState(dsFPDLedState_t state)
{
	hal_info("invoked.\n");

	if (state <= dsFPD_LED_DEVICE_NONE || state >= dsFPD_LED_DEVICE_MAX) {
		hal_err("Invalid parameter, state: %d.\n", state);
		return dsERR_INVALID_PARAM;
	}

	FPD_MUTEX_LOCK();
	if (!gIsFPDInitialized) {
		FPD_MUTEX_UNLOCK();
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}

	if (((1u << state) & gSupportedLEDStates) == 0u) {
		FPD_MUTEX_UNLOCK();
		hal_err("Requested LED state: %d is unsupported.\n", state);
		return dsERR_OPERATION_NOT_SUPPORTED;
	}

	/* API contract: dsFPSetLEDState should stop an in-progress indicator blink. */
	gCustomBlinkActive = false;

	if (applyLedStateLocked(state) != dsERR_NONE) {
		FPD_MUTEX_UNLOCK();
		return dsERR_GENERAL;
	}
	FPD_MUTEX_UNLOCK();
	hal_info("[LED_REQ] queued state=%d\n", state);
	return dsERR_NONE;
}

/**
 * @brief Gets the supported led states
 *
 * This function gets the supported led states
 *
 * @param[out] states - The bitwise value of all supported led states by the platform. refer ::dsFPDLedState_t
 *      e.g. *states = ((1<<dsFPD_LED_DEVICE_ACTIVE) | (1<<dsFPD_LED_DEVICE_STANDBY))
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 */
dsError_t dsFPGetSupportedLEDStates(unsigned int* states)
{
	hal_info("invoked.\n");
	if (states == NULL) {
		hal_err("Invalid parameter, states: %p.\n", states);
		return dsERR_INVALID_PARAM;
	}

	if (!isInitialized()) {
		hal_err("Module not initialized.\n");
		return dsERR_NOT_INITIALIZED;
	}
	hal_info("Supported LED states: 0x%X\n", gSupportedLEDStates);
	*states = gSupportedLEDStates;
	return dsERR_NONE;
}
