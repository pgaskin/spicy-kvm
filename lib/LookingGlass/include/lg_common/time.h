/**
 * Looking Glass
 * Copyright © 2017-2024 The Looking Glass Authors
 * https://looking-glass.io
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "lg_common/util.h"

#if defined(_WIN32)
#include <windows.h>

void windowsSetTimerResolution(void);
NTSYSCALLAPI NTSTATUS NTAPI NtDelayExecution(
  _In_ BOOLEAN Alertable,
  _In_opt_ PLARGE_INTEGER DelayInterval
);
#endif

typedef struct LGTimer LGTimer;

static inline uint64_t microtime(void)
{
#if defined(_WIN32)
  static unsigned long div = 0;
  if (unlikely(div == 0))
  {
    LARGE_INTEGER freq = { .QuadPart = 0LL };
    QueryPerformanceFrequency(&freq);
    div = freq.QuadPart / 1000000LL;
    if(div == 0)
      abort();
  }

  LARGE_INTEGER time;
  QueryPerformanceCounter(&time);
  return time.QuadPart / div;
#else
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (uint64_t)time.tv_sec * 1000000LL + time.tv_nsec / 1000LL;
#endif
}

static inline void nsleep(uint64_t ns)
{
#if defined(_WIN32)
  LARGE_INTEGER interval = { .QuadPart = -(int64_t)(ns / 100LL) };
  NtDelayExecution(FALSE, &interval);
#else
  const struct timespec ts =
  {
    .tv_sec  = ns / 1000000000,
    .tv_nsec = ns % 1000000000,
  };
  nanosleep(&ts, NULL);
#endif
}

static inline uint64_t nanotime(void)
{
#if defined(_WIN32)
  static double multiplier = 0.0;
  if (unlikely(!multiplier))
  {
    LARGE_INTEGER freq = { .QuadPart = 0LL };
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart == 0)
      abort();

    multiplier = 1e9 / freq.QuadPart;
  }

  LARGE_INTEGER time;
  QueryPerformanceCounter(&time);
  return (uint64_t) (time.QuadPart * multiplier);
#else
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
  return ((uint64_t)time.tv_sec * 1000000000LL) + time.tv_nsec;
#endif
}

static inline void tsDiff(struct timespec *diff, const struct timespec *left,
              const struct timespec *right)
{
  diff->tv_sec = left->tv_sec - right->tv_sec;
  diff->tv_nsec = left->tv_nsec - right->tv_nsec;
  if (diff->tv_nsec < 0)
  {
    --diff->tv_sec;
    diff->tv_nsec += 1000000000;
  }
}

static inline uint32_t __iter_div_u64_rem(uint64_t dividend, uint32_t divisor, uint64_t *remainder)
{
  uint32_t ret = 0;

  while (dividend >= divisor) {
    /* The following asm() prevents the compiler from
       optimising this loop into a modulo operation.  */
    asm("" : "+rm"(dividend));

    dividend -= divisor;
    ret++;
  }

  *remainder = dividend;

  return ret;
}

static inline void tsAdd(struct timespec *a, uint64_t ns)
{
  a->tv_sec  += __iter_div_u64_rem(a->tv_nsec + ns, 1000000000L, &ns);
  a->tv_nsec = ns;
}

typedef bool (*LGTimerFn)(void * udata);

bool lgCreateTimer(const unsigned int intervalMS, LGTimerFn fn,
    void * udata, LGTimer ** result);

void lgTimerDestroy(LGTimer * timer);
