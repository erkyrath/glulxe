/* iosstart.m: iOS-specific interface code for Glulx. (Objective C)
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

#import "TerpGlkViewController.h"
#import "TerpGlkDelegate.h"
#import "GlkStream.h"

#include "glk.h" /* This comes with the IosGlk library. */
#include "glulxe.h"
#include "iosglk_startup.h" /* This comes with the IosGlk library. */

void iosglk_startup_code()
{
	TerpGlkViewController *glkviewc = [TerpGlkViewController singleton];
	NSString *pathname = glkviewc.terpDelegate.gamePath;
	NSLog(@"iosglk_startup_code: game path is %@", pathname);
	
	gamefile = [[GlkStreamFile alloc] initWithMode:filemode_Read rock:1 unicode:NO textmode:NO dirname:@"." pathname:pathname];
	
	/* Now we have to check to see if it's a Blorb file. */
	int res;
	unsigned char buf[12];
	
	glk_stream_set_position(gamefile, 0, seekmode_Start);
	res = glk_get_buffer_stream(gamefile, (char *)buf, 12);
	if (!res) {
		init_err = "The data in this stand-alone game is too short to read.";
		return;
	}

	if (buf[0] == 'G' && buf[1] == 'l' && buf[2] == 'u' && buf[3] == 'l') {
		locate_gamefile(FALSE);
	}
	else if (buf[0] == 'F' && buf[1] == 'O' && buf[2] == 'R' && buf[3] == 'M'
			 && buf[8] == 'I' && buf[9] == 'F' && buf[10] == 'R' && buf[11] == 'S') {
		locate_gamefile(TRUE);
	}
	else {
		init_err = "This is neither a Glulx game file nor a Blorb file which contains one.";
	}
}
