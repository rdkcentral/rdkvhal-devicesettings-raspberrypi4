/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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

#ifdef DSHAL_ENABLE_SINGLETON_GUARD

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DSHAL_SINGLETON_LOCK_FILE "/run/lock/dshal_singleton.lock"

static int gDSHALSingletonLockFd = -1;

static void releaseSingletonLock(void)
{
	if (gDSHALSingletonLockFd >= 0) {
		struct flock lock = {0};
		lock.l_type = F_UNLCK;
		lock.l_whence = SEEK_SET;
		(void)fcntl(gDSHALSingletonLockFd, F_SETLK, &lock);
		(void)close(gDSHALSingletonLockFd);
		gDSHALSingletonLockFd = -1;
	}
}

__attribute__((constructor))
static void acquireSingletonLock(void)
{
	int fd;
	int len;
	char pidText[32];
	struct flock lock = {0};
	struct stat st;

	fd = open(DSHAL_SINGLETON_LOCK_FILE, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		(void)fprintf(stderr, "[DSHAL] Failed to open singleton lock file '%s': %s\n",
			DSHAL_SINGLETON_LOCK_FILE, strerror(errno));
		_exit(EXIT_FAILURE);
	}

	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		(void)fprintf(stderr, "[DSHAL] Singleton lock path is not a regular file: %s\n",
			DSHAL_SINGLETON_LOCK_FILE);
		(void)close(fd);
		_exit(EXIT_FAILURE);
	}

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	if (fcntl(fd, F_SETLK, &lock) != 0) {
		if (errno == EACCES || errno == EAGAIN) {
			(void)fprintf(stderr,
				"[DSHAL] Library singleton guard: '%s' is already loaded by another process.\n",
				DSHAL_SINGLETON_LOCK_FILE);
		} else {
			(void)fprintf(stderr, "[DSHAL] Failed to acquire singleton lock: %s\n", strerror(errno));
		}
		(void)close(fd);
		_exit(EXIT_FAILURE);
	}

	(void)ftruncate(fd, 0);
	len = snprintf(pidText, sizeof(pidText), "%ld\n", (long)getpid());
	if (len > 0) {
		(void)write(fd, pidText, (size_t)len);
	}

	gDSHALSingletonLockFd = fd;
}

__attribute__((destructor))
static void cleanupSingletonLock(void)
{
	releaseSingletonLock();
}

#endif
