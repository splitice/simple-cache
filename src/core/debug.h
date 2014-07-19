#if !defined(DEBUG_H_INCLUDED_AC1FB0B9_4189_4937_985B_0C6F757F7364)
#define DEBUG_H_INCLUDED_AC1FB0B9_4189_4937_985B_0C6F757F7364

#include <stdio.h>
#include <stdlib.h>
#include "config.h"

/* Based off p0f error handling macros */

#ifndef _HAVE_DEBUG_H
#define _HAVE_DEBUG_H
#ifdef _MSC_VER
//Just for intellisense
#define DEBUG(...) do {} while(0)
#define ERRORF(...) do {} while(0)
#define SAYF(...) do {} while(0)
#define FATAL(...) do {} while(0)
#define ABORT(...) do {} while(0)
#define PFATAL(...) do {} while(0)
#define PWARN(...) do {} while(0)
#else

#ifdef DEBUG_BUILD
#  define DEBUG(x...) fprintf(stderr, x)
#else
#  define DEBUG(x...) do {} while (0)
#endif /* ^DEBUG_BUILD */

#define ERRORF(x...)  fprintf(stderr, x)
#define SAYF(x...)    printf(x)

#define WARN(x...) do { \
    ERRORF("[!] WARNING: " x); \
    ERRORF("\n"); \
  } while (0)

#define FATAL(x...) do { \
    ERRORF("[-] PROGRAM ABORT : " x); \
    ERRORF("\n         Location : %s(), %s:%u\n\n", \
           __FUNCTION__, __FILE__, __LINE__); \
    exit(1); \
  } while (0)

#define ABORT(x...) do { \
    ERRORF("[-] PROGRAM ABORT : " x); \
    ERRORF("\n         Location : %s(), %s:%u\n\n", \
           __FUNCTION__, __FILE__, __LINE__); \
    abort(); \
  } while (0)

#define PFATAL(x...) do { \
    ERRORF("[-] SYSTEM ERROR : " x); \
    ERRORF("\n        Location : %s(), %s:%u\n", \
           __FUNCTION__, __FILE__, __LINE__); \
    perror("      OS message "); \
    ERRORF("\n"); \
    exit(1); \
  } while (0)

#define PWARN(x...) do { \
      ERRORF("[!] SYSTEM WARNING : " x); \
      ERRORF("\n        Location : %s(), %s:%u\n", \
             __FUNCTION__, __FILE__, __LINE__); \
      perror("      OS message "); \
      ERRORF("\n"); \
    } while (0)

#endif
#endif /* ! _HAVE_DEBUG_H */
#endif // !defined(DEBUG_H_INCLUDED_AC1FB0B9_4189_4937_985B_0C6F757F7364)
