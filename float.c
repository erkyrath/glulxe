/* float.c: Glulxe code for floating-point operations
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

#ifdef FLOAT_SUPPORT

/* This entire file is compiled out if the FLOAT_SUPPORT option is off.
   (Because we probably can't define a gfloat32 in that case.) */

/*### define non-cheesy versions */

glui32 encode_float(gfloat32 val)
{
    glui32 res;
    *(gfloat32 *)(&res) = val;
    return res;
}

gfloat32 decode_float(glui32 val)
{
    gfloat32 res;
    *(glui32 *)(&res) = val;
    return res;
}

#endif /* FLOAT_SUPPORT */
