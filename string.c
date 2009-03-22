/* string.c: Glulxe string and text functions.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

/* stream_num():
   Write a signed integer to the current output stream.
*/
void stream_num(glsi32 val)
{
  char buf[16];
  glui32 ival;
  int ix;

  if (val == 0) {
    glk_put_char('0');
    return;
  }

  if (val < 0) {
    glk_put_char('-');
    ival = -val;
  }
  else {
    ival = val;
  }

  ix = 0;
  while (ival != 0) {
    buf[ix] = (ival % 10) + '0';
    ix++;
    ival /= 10;
  }

  while (ix) {
    ix--;
    glk_put_char(buf[ix]);
  }
}

/* stream_hexnum():
   Write a signed integer to the current output stream.
*/
void stream_hexnum(glsi32 val)
{
  char buf[16];
  glui32 ival;
  int ix;

  if (val == 0) {
    glk_put_char('0');
    return;
  }

  if (val < 0) {
    glk_put_char('-');
    ival = -val;
  }
  else {
    ival = val;
  }

  ix = 0;
  while (ival != 0) {
    buf[ix] = (ival % 16) + '0';
    if (buf[ix] > '9')
      buf[ix] += ('A' - ('9' + 1));
    ix++;
    ival /= 16;
  }

  while (ix) {
    ix--;
    glk_put_char(buf[ix]);
  }
}

/* stream_string():
   Write a Glulx string object to the current output stream.
*/
void stream_string(glui32 addr)
{
  int ch;
  int type = Mem1(addr);
  addr++;

  if (type == 0xE1) {
    glk_put_string("###compressed-string###");
  }
  else if (type == 0xE0) {
    while (1) {
      ch = Mem1(addr);
      addr++;
      if (ch == '\0')
	break;
      glk_put_char(ch);
    }
  }
  else if (type >= 0xE0 && type <= 0xFF) {
    fatal_error("Attempt to print unknown type of string.");
  }
  else {
    fatal_error("Attempt to print non-string.");
  }
}

char *make_temp_string(glui32 addr)
{
  return "###string args not yet implemented###";
}

void free_temp_string(char *str)
{

}
