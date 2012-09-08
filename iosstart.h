
#include "glk.h"
#include "iosglk_startup.h"

/* This structure contains VM state which is not stored in a normal save file, but which is needed for an autorestore.
 
	(The reason it's not stored in a normal save file is that it's useless unless you serialize the entire Glk state along with the VM. Glulx normally doesn't do that, but for an iOS autosave, we do.)
 */
typedef struct library_state_data_struct {
	BOOL active;
	glui32 protectstart, protectend;
	glui32 iosys_mode, iosys_rock;
	glui32 stringtable;
} library_state_data;

extern void iosglk_do_autosave(void);
extern void iosglk_set_can_restart_flag(int);
extern int iosglk_can_restart_cleanly(void);
extern void iosglk_shut_down_process(void) GLK_ATTRIBUTE_NORETURN;
