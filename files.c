/* files.c: Glulxe file-handling code.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

/* is_gamefile_valid():
   Check guess what.
*/
int is_gamefile_valid()
{
  unsigned char buf[8];
  int res;
  glui32 version;

  glk_stream_set_position(gamefile, 0, seekmode_Start);
  res = glk_get_buffer_stream(gamefile, buf, 8);

  if (res != 8) {
    fatal_error("This is too short to be a valid Glulx file.");
    return FALSE;
  }

  if (buf[0] != 'G' || buf[1] != 'l' || buf[2] != 'u' || buf[3] != 'l') {
    fatal_error("This is not a valid Glulx file.");
    return FALSE;
  }

  version = Read4(buf+4);
  if (version < 0x10000) {
    fatal_error("This Glulx file is too old a version to execute.");
    return FALSE;
  }
  if (version >= 0x20000) {
    fatal_error("This Glulx file is too new a version to execute.");
    return FALSE;
  }

  return TRUE;
}

