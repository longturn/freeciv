/********************************************************************** 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/* Random number initialization, possibly system-dependant */

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include "fc_prehdrs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "log.h"

/**************************************************************************
  Read a 32-bit random value using getentropy(), if available.
  (Linux, FreeBSD, OpenBSD, macOS)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_getentropy(uint32_t *ret)
{
#if HAVE_GETENTROPY
  /* getentropy() is from OpenBSD, and should be supported on at least
   * FreeBSD and glibc on Linux (as a wrapper to getrandom()) as well.
   */
  uint32_t seed = 0;
  
  if (getentropy(&seed, 4) == 0) {
    *ret = seed;
    return TRUE;
  } else {
    log_error("getentropy() failed: %s", strerror(errno));
  }
#endif
  return FALSE;
}

/**************************************************************************
  Read a 32-bit random value using /dev/urandom, if available. 
  (Most Unix-like systems)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_urandom(uint32_t *ret)
{
#if HAVE__DEV_URANDOM   /* the first slash turns to an extra underline */
  /*
   * /dev/urandom should be available on most Unixen. The Wikipedia page
   * mentions Linux, FreeBSD, OpenBSD, macOS as well as Solaris, NetBSD,
   * Tru64 UNIX 5.1B, AIX 5.2 and HP-UX 11i v2. 
   *
   * However, opening it may fail if the game is running in a chroot without
   * it, or is otherwise restricted from accessing it. This is why getentropy()
   * is preferred. 
   */
  static const char *random_device = "/dev/urandom";
  int fd = 0;
  uint32_t seed = 0;
  bool ok = FALSE;
  
  /* stdio would read an unnecessarily large block, which may mess up users
   * /dev/random on Linux, so use the low-level calls instead. */
  fd = open(random_device, O_RDONLY);
  if (fd == -1) {
    log_error("Opening %s failed: %s", random_device, strerror(errno));
  } else {
    int n = read(fd, &seed, 4);
    
    if (n == -1) {
      log_error("Reading %s failed: %s", random_device, strerror(errno));
    } else if (n != 4) {
      log_error("Reading %s: short read without error", random_device);
    } else {
      ok = 1;
    }
    close(fd);
  }
  if (ok) {
    *ret = seed;
    return TRUE;
  }
#endif
  return FALSE;
}

/**************************************************************************
  Generate a 32-bit random-ish value from the current time, using
  clock_gettime(), if available. (POSIX-compatible systems.)
  Return FALSE on error, otherwise return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_clock_gettime(uint32_t *ret)
{
#if HAVE_CLOCK_GETTIME
  /* 
   * clock_gettime() nominally gives nanoseconds, but the real granularity may be
   * worse, making the lowest bits less useful. On the other hand, the lower bits
   * of full seconds are the most useful, the high bits being very predictable.
   * Xor them together to hopefully get something relatively unpredictable in the
   * bottom 30 bits. 
   */
  uint32_t seed = 0;
  struct timespec tp;

  if (clock_gettime(CLOCK_REALTIME, &tp) == 0) {
    seed  = tp.tv_nsec;
    seed ^= tp.tv_sec;
    *ret = seed;
    return TRUE;
  } else {
    /* This shouldn't fail if the function is supported on the system */
    log_error("clock_gettime(CLOCK_REALTIME) failed: %s\n", strerror(errno));
  }
#endif
  return FALSE;
}

/**************************************************************************
  Generate a lousy 32-bit random-ish value from the current time.
  Return TRUE and store seed in *ret.
**************************************************************************/
static bool generate_seed_time(uint32_t *ret)
{
  /* No reasonable way this can fail */
  *ret = (uint32_t) time(NULL);
  return TRUE;
}


/**************************************************************************
  Return a random 32-bit value to use as game seed, by whatever means
  the underlying system provides. 
**************************************************************************/
unsigned int generate_game_seed(void)
{
  uint32_t seed = 0;
  
  /* Good random sources */
  if (generate_seed_getentropy(&seed)) {
    log_normal("Got random seed from getentropy()");
    return seed;
  }
  if (generate_seed_urandom(&seed)) {
    log_normal("Got random seed from urandom()");
    return seed;
  }
  /* Not so good random source */
  log_normal("No good random source usable. Falling back to time-based random seeding.");
  if (generate_seed_clock_gettime(&seed)) {
    log_normal("Got random seed with clock_gettime()");
    return seed;
  }
  /* Lousy last-case source */
  log_error("Warning: Falling back to predictable random seed from current coarse-granularity time.");
  generate_seed_time(&seed);
  log_normal("Got random seed from time()");
  return seed;
}

