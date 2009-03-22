/* macstart.c: Macintosh-specific code for Glulxe.
 */

#include "glk.h"
#include "gi_dispa.h"
#include "gi_blorb.h"

#include "macglk_startup.h" /* This comes with the MacGlk library. */

static OSType gamefile_type = 'UlxG';
extern strid_t gamefile; /* This is defined in glulxe.h. */

Boolean macglk_startup_code(macglk_startup_t *data)
{
  giblorb_err_t err;
  
  data->app_creator = 'gUlx';
  data->startup_model = macglk_model_ChooseOrBuiltIn;
  data->gamefile_types = &gamefile_type;
  data->savefile_type = 'IFZS';
  data->datafile_type = 'UlxD';
  data->num_gamefile_types = 1;
  data->gamefile = &gamefile;
  
  return TRUE;
}
