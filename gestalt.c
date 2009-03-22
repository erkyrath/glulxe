/* gestalt.c: Glulxe code for gestalt selectors
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"
#include "gestalt.h"

glui32 do_gestalt(glui32 val, glui32 val2)
{
  switch (val) {

  case gestulx_GlulxVersion:
    return 0x00010000; /* Glulx spec version 1.0 */

  case gestulx_TerpVersion:
    return 0x00000302; /* Glulxe version 0.3.2 */

  case gestulx_ResizeMem:
    return 1; /* We can handle setmemsize. */

  case gestulx_Undo:
    return 1; /* We can handle saveundo and restoreundo. */

  case gestulx_Glk:
    return 1; /* A Glk library is hooked up. */


  default:
    return 0;

  }
}
