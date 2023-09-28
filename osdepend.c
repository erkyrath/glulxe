/* osdepend.c: Glulxe platform-dependent code.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

/* This file contains definitions for platform-dependent code. Since
   Glk takes care of I/O, this is a short list -- memory allocation
   and random numbers.

   The Makefile (or whatever) should define OS_UNIX, or some other
   symbol. Code contributions welcome. 
*/


/* We have a slightly baroque random-number scheme. If the Glulx
   @setrandom opcode is given seed 0, we use "true" randomness, from a
   platform native RNG if possible. If @setrandom is given a nonzero
   seed, we use a simple Mersenne Twister RNG (provided below). The
   use of a provided algorithm aids cross-platform testing and debugging.
   (Those being the cases where you'd set a nonzero seed.)

   To define a native RNG, define the macros RAND_SET_SEED() (seed the
   RNG with the clock or some other truly random source) and RAND_GET()
   (grab a number). Note that RAND_SET_SEED() does not take an argument;
   it is only called when seed=0.

   If RAND_SET_SEED/RAND_GET are not provided, we call back to the same
   Mersenne RNG as before, but seeded from the system clock.
*/

static glui32 mt_random(void);
static void mt_seed_random(glui32 seed);

#ifdef OS_UNIX

#include <time.h>
#include <stdlib.h>

/* Allocate a chunk of memory. */
void *glulx_malloc(glui32 len)
{
  return malloc(len);
}

/* Resize a chunk of memory. This must follow ANSI rules: if the
   size-change fails, this must return NULL, but the original chunk 
   must remain unchanged. */
void *glulx_realloc(void *ptr, glui32 len)
{
  return realloc(ptr, len);
}

/* Deallocate a chunk of memory. */
void glulx_free(void *ptr)
{
  free(ptr);
}

/* Use POSIX random() as the native RNG, seeded from the POSIX clock. */
#define RAND_SET_SEED() (srandom(time(NULL)))
#define RAND_GET() (random())

#endif /* OS_UNIX */

#ifdef OS_WINDOWS

#ifdef _MSC_VER /* For Visual C++, get rand_s() */
#define _CRT_RAND_S
#endif

#include <time.h>
#include <stdlib.h>

/* Allocate a chunk of memory. */
void *glulx_malloc(glui32 len)
{
  return malloc(len);
}

/* Resize a chunk of memory. This must follow ANSI rules: if the
   size-change fails, this must return NULL, but the original chunk 
   must remain unchanged. */
void *glulx_realloc(void *ptr, glui32 len)
{
  return realloc(ptr, len);
}

/* Deallocate a chunk of memory. */
void glulx_free(void *ptr)
{
  free(ptr);
}

#ifdef _MSC_VER /* Visual C++ */

/* Do nothing, as rand_s() has no seed. */
static void msc_srandom()
{
}

/* Use the Visual C++ function rand_s() as the native RNG.
   This calls the OS function RtlGetRandom(). */
static glui32 msc_random()
{
  glui32 value;
  rand_s(&value);
  return value;
}

#define RAND_SET_SEED() (msc_srandom())
#define RAND_GET() (msc_random())

#else /* Other Windows compilers */

/* Use our Mersenne Twister as the native RNG, but seed it from
   the clock. */
#define RAND_SET_SEED() (mt_seed_random(time(NULL)))
#define RAND_GET() (mt_random())

#endif

#endif /* OS_WINDOWS */


/* If no native RNG is defined above, use the Mersenne implementation. */
#ifndef RAND_SET_SEED
#define RAND_SET_SEED() (mt_seed_random(time(NULL)))
#define RAND_GET() (mt_random())
#endif /* RAND_SET_SEED */

static int rand_use_native = TRUE;

/* Set the random-number seed, and also select which RNG to use.
*/
void glulx_setrandom(glui32 seed)
{
    if (seed == 0) {
        rand_use_native = TRUE;
        RAND_SET_SEED();
    }
    else {
        rand_use_native = FALSE;
        mt_seed_random(seed);
    }
}

/* Return a random number in the range 0 to 2^32-1. */
glui32 glulx_random()
{
    if (rand_use_native) {
        return RAND_GET();
    }
    else {
        return mt_random();
    }
}


/* This is the Mersenne Twister MT19937 random-number generator and seed function. */
static glui32 mt_random(void);
static void mt_seed_random(glui32 seed);

#define MT_N (624)
#define MT_M (397)
#define MT_A (0x9908B0DF)
#define MT_F (1812433253)
/* Also width=32 */
static glui32 mt_table[MT_N]; /* State for the RNG. */
static int mt_index;

static void mt_seed_random(glui32 seed)
{
    int i;

    mt_table[0] = seed;
    for (i=1; i<MT_N; i++) {
        mt_table[i] = ((MT_F * (mt_table[i-1] ^ (mt_table[i-1] >> 30)) + i));
    }
    
    mt_index = MT_N+1;
}

static glui32 mt_random()
{
    int i;
    glui32 y;
    
    if (mt_index >= MT_N) {
        /* Do the twist */
        for (i=0; i<MT_N; i++) {
            glui32 x, xa;
            x = (mt_table[i] & 0x80000000) | (mt_table[(i + 1) % MT_N] & 0x7FFFFFFF);
            xa = x >> 1;
            if (x % 2) {
                xa = xa ^ MT_A;
            }
            mt_table[i] = mt_table[(i+MT_M) % MT_N] ^ xa;
        }
        mt_index = 0;
    }
    
    y = mt_table[mt_index];
    /* These values are called (u, d, s, b, t, c) in the Mersenne Twister algorithm. */
    y = y ^ ((y >> 11) & 0xFFFFFFFF);
    y = y ^ ((y << 7) & 0x9D2C5680);
    y = y ^ ((y << 15) & 0xEFC60000);
    y = y ^ (y >> 18);
    mt_index += 1;
    return y;
}


#include <stdlib.h>

/* I'm putting a wrapper for qsort() here, in case I ever have to
   worry about a platform without it. But I am not worrying at
   present. */
void glulx_sort(void *addr, int count, int size, 
  int (*comparefunc)(void *p1, void *p2))
{
  qsort(addr, count, size, (int (*)(const void *, const void *))comparefunc);
}

#ifdef FLOAT_SUPPORT
#include <math.h>

#ifdef FLOAT_COMPILE_SAFER_POWF

/* This wrapper handles all special cases, even if the underlying
   powf() function doesn't. */
gfloat32 glulx_powf(gfloat32 val1, gfloat32 val2)
{
  if (val1 == 1.0f)
    return 1.0f;
  else if ((val2 == 0.0f) || (val2 == -0.0f))
    return 1.0f;
  else if ((val1 == -1.0f) && isinf(val2))
    return 1.0f;
  return powf(val1, val2);
}

#else /* FLOAT_COMPILE_SAFER_POWF */

/* This is the standard powf() function, unaltered. */
gfloat32 glulx_powf(gfloat32 val1, gfloat32 val2)
{
  return powf(val1, val2);
}

#endif /* FLOAT_COMPILE_SAFER_POWF */

#endif /* FLOAT_SUPPORT */
