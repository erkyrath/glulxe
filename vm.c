/* vm.c: Glulxe code related to the VM overall. Also miscellaneous stuff.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

/* The memory blocks which contain VM main memory and the stack. */
unsigned char *memmap = NULL;
unsigned char *stack = NULL;

/* Various memory addresses which are useful. These are loaded in from
   the game file header. */
glui32 ramstart;
glui32 endgamefile;
glui32 origendmem;
glui32 stacksize;
glui32 startfuncaddr;
glui32 checksum;

/* The VM registers. */
glui32 stackptr;
glui32 frameptr;
glui32 pc;
glui32 stringtable;
glui32 valstackbase;
glui32 localsbase;
glui32 endmem;

/* setup_vm():
   Read in the game file and build the machine, allocating all the memory
   necessary.
*/
void setup_vm()
{
  unsigned char buf[4 * 7];
  int res;

  pc = 0; /* Clear this, so that error messages are cleaner. */

  /* Read in all the size constants from the game file header. */

  glk_stream_set_position(gamefile, 8, seekmode_Start);
  res = glk_get_buffer_stream(gamefile, (char *)buf, 4 * 7);
  
  ramstart = Read4(buf+0);
  endgamefile = Read4(buf+4);
  origendmem = Read4(buf+8);
  stacksize = Read4(buf+12);
  startfuncaddr = Read4(buf+16);
  stringtable = Read4(buf+20);
  checksum = Read4(buf+24);

  /* Do a few sanity checks. */

  if ((ramstart & 0xFF)
    || (endgamefile & 0xFF) 
    || (origendmem & 0xFF)
    || (stacksize & 0xFF)) {
    nonfatal_warning("One of the segment boundaries in the header is not "
      "256-byte aligned.");
  }

  if (ramstart < 0x100 || endgamefile < ramstart || origendmem < endgamefile) {
    fatal_error("The segment boundaries in the header are in an impossible "
      "order.");
  }
  if (stacksize < 0x100) {
    fatal_error("The stack size in the header is too small.");
  }
  
  /* Allocate main memory and the stack. This is where memory allocation
     errors are most likely to occur. */

  endmem = origendmem;
  memmap = (unsigned char *)glulx_malloc(origendmem);
  if (!memmap) {
    fatal_error("Unable to allocate Glulx memory space.");
  }
  stack = (unsigned char *)glulx_malloc(stacksize);
  if (!stack) {
    glulx_free(memmap);
    memmap = NULL;
    fatal_error("Unable to allocate Glulx stack space.");
  }

  /* Initialize various other things in the terp. */
  init_operands(); 
  init_serial();

  /* Set up the initial machine state. */
  vm_restart();
}

/* finalize_vm():
   Deallocate all the memory and shut down the machine.
*/
void finalize_vm()
{
  if (memmap) {
    glulx_free(memmap);
    memmap = NULL;
  }
  if (stack) {
    glulx_free(stack);
    stack = NULL;
  }
}

/* vm_restart(): 
   Put the VM into a state where it's ready to begin executing the
   game. This is called both at startup time, and when the machine
   performs a "restart" opcode. 
*/
void vm_restart()
{
  glui32 lx;
  int res;

  /* Reset memory to the original size. */
  lx = change_memsize(origendmem);
  if (lx)
    fatal_error("Memory could not be reset to its original size.");

  /* Load in all of main memory */ /* ###protect */
  glk_stream_set_position(gamefile, 0, seekmode_Start);
  for (lx=0; lx<endgamefile; lx++) {
    res = glk_get_char_stream(gamefile);
    if (res == -1) {
      fatal_error("The game file ended unexpectedly.");
    }
    memmap[lx] = res;
  }
  for (lx=endgamefile; lx<origendmem; lx++) {
    memmap[lx] = 0;
  }

  /* Reset all the registers */
  stackptr = 0;
  frameptr = 0;
  pc = 0;
  /* ### stringtable? */
  valstackbase = 0;
  localsbase = 0;

  /* Push the first function call. (No arguments.) */
  enter_function(startfuncaddr, 0, NULL);

  /* We're now ready to execute. */
}

/* change_memsize():
   Change the size of the memory map. This may not be available at
   all; #define FIXED_MEMSIZE if you want the interpreter to
   unconditionally refuse.
   Returns 0 for success; otherwise, the operation failed.
*/
glui32 change_memsize(glui32 newlen)
{
  long lx;

  if (newlen == endmem)
    return 0;

#ifdef FIXED_MEMSIZE
  return 1;
#else /* FIXED_MEMSIZE */

  if (newlen < origendmem)
    fatal_error("Cannot resize Glulx memory space smaller than it started.");
  
  memmap = (unsigned char *)glulx_realloc(memmap, newlen);
  if (!memmap) {
    fatal_error("Unable to resize Glulx memory space.");
  }

  if (newlen > endmem) {
    for (lx=endmem; lx<newlen; lx++) {
      memmap[lx] = 0;
    }
  }

  endmem = newlen;

  return 0;

#endif /* FIXED_MEMSIZE */
}

/* pop_arguments():
   Pop N arguments off the stack, and put them in an array. 
   At the moment, we have a fixed-size array of 32 elements. Really
   it should be dynamically expandable. 
*/
glui32 *pop_arguments(glui32 count)
{
  int ix;
  glui32 argptr;

  #define MAXARGS (32)
  static glui32 array[MAXARGS];

  if (count > MAXARGS)
    fatal_error("Interpreter bug -- unable to handle more than 32 "
      "function arguments.");

  if (stackptr < valstackbase+4*count) 
    fatal_error("Stack underflow in arguments.");

  stackptr -= 4*count;

  for (ix=0; ix<count; ix++) {
    argptr = stackptr+4*((count-1)-ix);
    array[ix] = Stk4(argptr);
  }

  return array;
}

