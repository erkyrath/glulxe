/* unixautosave.c: Unix-specific autosave code for Glulxe.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

#include <stdio.h>
#include "glulxe.h"
#include "glkstart.h" /* This comes with the Glk library. */

/* Backtrack through the current opcode (at prevpc), and figure out whether its input arguments are on the stack or not. This will be important when setting up the saved VM state for restarting its opcode.
 
    The opmodes argument must be an array int[3]. Returns TRUE on success.
 */
static int parse_partial_operand(int *opmodes)
{
    glui32 addr = prevpc;
    
    /* Fetch the opcode number. */
    glui32 opcode = Mem1(addr);
    addr++;
    if (opcode & 0x80) {
        /* More than one-byte opcode. */
        if (opcode & 0x40) {
            /* Four-byte opcode */
            opcode &= 0x3F;
            opcode = (opcode << 8) | Mem1(addr);
            addr++;
            opcode = (opcode << 8) | Mem1(addr);
            addr++;
            opcode = (opcode << 8) | Mem1(addr);
            addr++;
        }
        else {
            /* Two-byte opcode */
            opcode &= 0x7F;
            opcode = (opcode << 8) | Mem1(addr);
            addr++;
        }
    }
    
    if (opcode != 0x130) { /* op_glk */
        /* Error: parsed wrong opcode */
        return FALSE;
    }
    
    /* @glk has operands LLS. */
    opmodes[0] = Mem1(addr) & 0x0F;
    opmodes[1] = (Mem1(addr) >> 4) & 0x0F;
    opmodes[2] = Mem1(addr+1) & 0x0F;
    
    return TRUE;
}

void glkunix_do_autosave(glui32 eventaddr)
{
    printf("###auto glkunix_do_autosave(%d)\n", eventaddr);

    /* When the save file is autorestored, the VM will restart the @glk opcode. That means that the Glk argument (the event structure address) must be waiting on the stack. Possibly also the @glk opcode's operands -- these might or might not have come off the stack. */
    int res;
    int opmodes[3];
    res = parse_partial_operand(opmodes);
    if (!res)
        return;

    //###auto: determine save.glksave and save.json paths
    strid_t savefile = glkunix_stream_open_pathname_gen("autosave.glksave", TRUE, FALSE, 1);
    if (!savefile)
        return;
        
    /* Push all the necessary arguments for the @glk opcode. */
    glui32 origstackptr = stackptr;
    int stackvals = 0;
    /* The event structure address: */
    stackvals++;
    if (stackptr+4 > stacksize)
        fatal_error("Stack overflow in autosave callstub.");
    StkW4(stackptr, eventaddr);
    stackptr += 4;
    if (opmodes[1] == 8) {
        /* The number of Glk arguments (1): */
        stackvals++;
        if (stackptr+4 > stacksize)
            fatal_error("Stack overflow in autosave callstub.");
        StkW4(stackptr, 1);
        stackptr += 4;
    }
    if (opmodes[0] == 8) {
        /* The Glk call selector (0x00C0): */
        stackvals++;
        if (stackptr+4 > stacksize)
            fatal_error("Stack overflow in autosave callstub.");
        StkW4(stackptr, 0x00C0); /* glk_select */
        stackptr += 4;
    }
    
    /* Push a temporary callstub which contains the *last* PC -- the address of the @glk(select) invocation. */
    if (stackptr+16 > stacksize)
        fatal_error("Stack overflow in autosave callstub.");
    StkW4(stackptr+0, 0);
    StkW4(stackptr+4, 0);
    StkW4(stackptr+8, prevpc);
    StkW4(stackptr+12, frameptr);
    stackptr += 16;
    
    res = perform_save(savefile);
    
    stackptr -= 16; // discard the temporary callstub
    stackptr -= 4 * stackvals; // discard the temporary arguments
    if (origstackptr != stackptr)
        fatal_error("Stack pointer mismatch in autosave");
    
    glk_stream_close(savefile, NULL);
    savefile = NULL;

    if (res) {
        return;
    }
    printf("### perform_save succeeded\n");

}

