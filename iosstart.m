/* iosstart.m: iOS-specific interface code for Glulx. (Objective C)
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

#import "TerpGlkViewController.h"
#import "TerpGlkDelegate.h"
#import "GlkLibrary.h"
#import "GlkStream.h"

#include "glk.h" /* This comes with the IosGlk library. */
#include "glulxe.h"
#include "iosstart.h"
#include "iosglk_startup.h" /* This comes with the IosGlk library. */

static library_state_data library_state; /* used by the archive/unarchive hooks */

static void iosglk_game_start(void);
static void iosglk_game_select(void);
static void stash_library_state(void);
static int recover_library_state(void);
static void iosglk_library_archive(NSCoder *encoder);
static void iosglk_library_unarchive(NSCoder *encoder);

/* We don't load in the game file here. Instead, we set a hook which glk_main() will call back to do that. Why? Because of the annoying restartability of the VM under iosglk; we may finish glk_main() and then have to call it again.
 */
void iosglk_startup_code()
{
	set_library_start_hook(&iosglk_game_start);
	set_library_select_hook(&iosglk_game_select);
	max_undo_level = 32; // allow 32 undo steps
}

/* This is the library_start_hook, which will be called every time glk_main() begins.
 */
static void iosglk_game_start()
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

/* This is the library_select_hook, which will be called every time glk_select() is invoked.
 */
static void iosglk_game_select()
{
	NSLog(@"### game called select");
	//### filter based on whether the last event was important?
	
	iosglk_do_autosave();
}

static NSString *documents_dir() {
	/* We use an old-fashioned way of locating the Documents directory. (The NSManager method for this is iOS 4.0 and later.) */
	
	NSArray *dirlist = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
	if (!dirlist || [dirlist count] == 0) {
		NSLog(@"unable to locate Documents directory.");
		return nil;
	}
	
	return [dirlist objectAtIndex:0];
}

void iosglk_do_autosave()
{
	GlkLibrary *library = [GlkLibrary singleton];
	NSLog(@"### attempting autosave (pc = %x)", pc);

	NSString *dirname = documents_dir();
	if (!dirname)
		return;
	NSString *tmpgamepath = [dirname stringByAppendingPathComponent:@"autosave-tmp.glksave"];
	
	GlkStreamFile *savefile = [[GlkStreamFile alloc] initWithMode:filemode_Write rock:1 unicode:NO textmode:NO dirname:dirname pathname:tmpgamepath];
	//### rejigger pc to point to the @select opcode?
	push_callstub(0, 0);
	int res = perform_save(savefile);
	pop_callstub(0);
	glk_stream_close(savefile, nil);
	
	if (res) {
		NSLog(@"VM autosave failed!");
		return;
	}
	
	bzero(&library_state, sizeof(library_state));
	stash_library_state();
	/* The iosglk_library_archive hook will write out the contents of library_state. */
	
	NSString *tmplibpath = [dirname stringByAppendingPathComponent:@"autosave-tmp.plist"];
	[GlkLibrary setExtraArchiveHook:iosglk_library_archive];
	res = [NSKeyedArchiver archiveRootObject:library toFile:tmplibpath];
	[GlkLibrary setExtraArchiveHook:nil];

	if (!res) {
		NSLog(@"library serialize failed!");
		return;
	}

	NSString *finalgamepath = [dirname stringByAppendingPathComponent:@"autosave.glksave"];
	NSString *finallibpath = [dirname stringByAppendingPathComponent:@"autosave.plist"];
	
	/* This is not really atomic, but we're already past the serious failure modes. */
	[library.filemanager removeItemAtPath:finallibpath error:nil];
	[library.filemanager removeItemAtPath:finalgamepath error:nil];
	
	res = [library.filemanager moveItemAtPath:tmpgamepath toPath:finalgamepath error:nil];
	if (!res) {
		NSLog(@"could not move game autosave to final position!");
		return;
	}
	res = [library.filemanager moveItemAtPath:tmplibpath toPath:finallibpath error:nil];
	if (!res) {
		NSLog(@"could not move library autosave to final position");
		return;
	}
}

static void stash_library_state()
{
	library_state.active = YES;
	//###
}

static int recover_library_state()
{
	if (library_state.active) {
		//###
	}
	
	return YES;
}

static void iosglk_library_archive(NSCoder *encoder) {
	if (library_state.active) {
		//###
	}
}

int iosglk_can_restart_cleanly()
{
 	return vm_exited_cleanly;
}

void iosglk_shut_down_process()
{
	/* Yes, we really do want to exit the app here. A fatal error has occurred at the interpreter level, so we can't restart it cleanly. The user has either hit a "goodbye" dialog button or the Home button; either way, it's time for suicide. */
	NSLog(@"iosglk_shut_down_process: goodbye!");
	exit(1);
}

