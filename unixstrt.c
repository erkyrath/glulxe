/* unixstrt.c: Unix-specific code for Glulxe.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glkstart.h" /* This comes with the Glk library. */

extern strid_t gamefile; /* This is defined in main.c. */

/* The only command-line argument is the filename. */
glkunix_argumentlist_t glkunix_arguments[] = {
    { "", glkunix_arg_ValueFollows, "filename: The game file to load." },
    { NULL, glkunix_arg_End, NULL }
};

int glkunix_startup_code(glkunix_startup_t *data)
{
    char *cx;

    if (data->argc <= 1)
        return FALSE;
    cx = data->argv[1];
    
    gamefile = glkunix_stream_open_pathname(cx, FALSE, 1);
    if (!gamefile)
        return FALSE;
        
    return TRUE; 
}

