
#include "glk.h"
#include "iosglk_startup.h"

typedef struct library_state_data_struct {
	BOOL active;
	int dummy;
} library_state_data;

extern void iosglk_do_autosave(void);
extern void iosglk_set_can_restart_flag(int);
extern int iosglk_can_restart_cleanly(void);
extern void iosglk_shut_down_process(void) GLK_ATTRIBUTE_NORETURN;
