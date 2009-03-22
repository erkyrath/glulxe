/* osdepend.c: Glulxe platform-dependent code.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

/* This file contains definitions for platform-dependent code. Since
   Glk takes care of I/O, this is a short list -- memory allocation
   and random numbers.

   The Makefile (or whatever) should define OS_UNIX, or some other
   symbol. Code contributions welcome. 
*/

#ifdef OS_UNIX

#include <time.h>
#include <stdlib.h>

/* Allocate a chunk of memory. */
void *glulx_malloc(glui32 len)
{
  return malloc(len);
}

/* Resize a chunk of memory. */
void *glulx_realloc(void *ptr, glui32 len)
{
  return realloc(ptr, len);
}

/* Deallocate a chunk of memory. */
void glulx_free(void *ptr)
{
  free(ptr);
}

/* Set the random-number seed; zero means use as random a source as
   possible. */
void glulx_setrandom(glui32 seed)
{
  if (seed == 0)
    seed = time(NULL);
  srandom(seed);
}

/* Return a random number in the range 0 to 2^32-1. */
glui32 glulx_random()
{
  return random();
}

#endif
