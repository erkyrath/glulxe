/* unixautosave.c: Unix-specific autosave code for Glulxe.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

#include <stdio.h>
#include "glulxe.h"
#include "glkstart.h" /* This comes with the Glk library. */

void glkunix_do_autosave(glui32 eventaddr)
{
    printf("### glkunix_do_autosave(%d)\n", eventaddr);
}

