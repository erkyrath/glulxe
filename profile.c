/* profile.c: Glulxe profiling functions.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

/* 
If compiled in, these functions maintain a collection of profiling
information as the Glulx program executes.

The profiling code is not smart about VM operations that rearrange the
call stack. In fact, it's downright stupid. @restart, @restore,
@restoreundo, or @throw will kill the interpreter.

On a normal VM exit (end of top-level routine or @quit), the profiler
writes out a data file called "profile-raw". This is an XML file of
the form

<profile>
  <function ... />
  <function ... />
  ...
</profile>

The function list includes every function which was called during the
program's run. Each function tag includes the following attributes:
 
  addr=HEX:         The VM address of the function (in hex).
  call_count=INT:   The number of times the function was called.
  accel_count=INT:  The number of times the function was called with
    acceleration.
  total_time=FLOAT: The amount of time spent during all calls to the
    function (in seconds, as a floating-point value).
  total_ops=INT:    The number of opcodes executed during all calls to
    the function.
  self_time=FLOAT:  The amount of time spent during all calls to the
    function, excluding time spent in subcalls (functions called *by* the
    function).
  self_ops=INT:     The number of opcodes executed during all calls to
    the function, excluding time spent in subcalls.

Note that if a function does not make any function calls, total_time
will be the same as self_time (and total_ops the same as self_ops).

Two special function entries may be included. The function with address
"1" (which is not a legal Glulx function address) represents time spent
in @glk opcode calls. This will typically have a large self_time, 
because it includes all the time spent waiting for input.

The function with address "2" represents the time spent printing string
data (the @streamchar, @streamunichar, @streamnum, and @streamstr
opcodes).

(Both "1" and "2" represent time spent in the Glk library, but they
get there by different code paths.)

The function with the lowest address (ignoring "1" and "2") is the
top-level Main__() function generated by the compiler. Its total_time
is the running time of the entire program, and its total_ops is the
number of opcodes executed by the entire program.

 */

#include "glk.h"
#include "glulxe.h"

#if VM_PROFILING

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/* Set if the --profile switch is used. */
static int profiling_active = FALSE;
static char *profiling_filename = NULL;
static strid_t profiling_stream = NULL;

typedef struct function_struct {
  glui32 addr;

  glui32 call_count;
  glui32 accel_count;
  glui32 entry_depth;
  struct timeval entry_start_time;
  glui32 entry_start_op;
  struct timeval total_time;
  glui32 total_ops;
  struct timeval self_time;
  glui32 self_ops;

  struct function_struct *hash_next;
} function_t;

typedef struct frame_struct {
  struct frame_struct *parent;
  function_t *func;

  struct timeval entry_time;
  glui32 entry_op;

  struct timeval children_time;
  glui32 children_ops;
} frame_t;

#define FUNC_HASH_SIZE (511)

static function_t **functions = NULL;
static frame_t *current_frame = NULL;

/* This counter is globally visible, because the profile_tick() macro
   increments it. */
glui32 profile_opcount = 0;

/* This is called from the setup code -- glkunix_startup_code(), for the
   Unix version. If called, the interpreter will keep profiling information,
   and write it out at shutdown time. If this is not called, the interpreter
   will skip all the profiling code. (Although it won't be quite as fast
   as if the VM_PROFILING symbol were compiled out entirely.)

   The arguments are a little tricky, because I developed this on Unix,
   but I want it to remain accessible on all platforms. Pass a writable
   stream object as the first argument; at game-shutdown time, the terp
   will write the profiling data to this object and then close it.

   However, if it's not convenient to open a stream in the startup code,
   you can simply pass a filename as the second argument. This filename
   will be opened according to the usual Glk data file rules, which means
   it may wind up in a sandboxed data directory. The filename should not
   contain slashes or other pathname separators.

   If you pass NULL for both arguments, a file called "profile-raw" will
   be written.
*/
void setup_profile(strid_t stream, char *filename)
{
  profiling_active = TRUE;

  if (stream)
    profiling_stream = stream;
  else if (filename)
    profiling_filename = filename;
  else
    profiling_filename = "profile-raw";
}

int init_profile()
{
  int bucknum;

  if (!profiling_active)
    return TRUE;

  functions = (function_t **)glulx_malloc(FUNC_HASH_SIZE
    * sizeof(function_t *));
  if (!functions) 
    return FALSE;

  for (bucknum=0; bucknum<FUNC_HASH_SIZE; bucknum++) 
    functions[bucknum] = NULL;

  return TRUE;
}

static function_t *get_function(glui32 addr)
{
  int bucknum = (addr % FUNC_HASH_SIZE);
  function_t *func;

  for (func = (functions[bucknum]); 
       func && func->addr != addr;
       func = func->hash_next) { }

  if (!func) {
    func = (function_t *)glulx_malloc(sizeof(function_t));
    if (!func)
      fatal_error("Profiler: cannot malloc function.");
    memset(func, 0, sizeof(function_t));
    func->hash_next = functions[bucknum];
    functions[bucknum] = func;

    func->addr = addr;
    func->call_count = 0;
    func->accel_count = 0;
    timerclear(&func->entry_start_time);
    func->entry_start_op = 0;
    timerclear(&func->total_time);
    func->total_ops = 0;
    timerclear(&func->self_time);
    func->self_ops = 0;
  }

  return func;
}

static char *timeprint(struct timeval *tv, char *buf)
{
  sprintf(buf, "%ld.%.6ld", (long)tv->tv_sec, (long)tv->tv_usec);
  return buf;
}

void profile_in(glui32 addr, int accel)
{
  frame_t *fra;
  function_t *func;
  struct timeval now;

  if (!profiling_active)
    return;

  /* printf("### IN: %lx%s\n", addr, (accel?" accel":"")); */

  gettimeofday(&now, NULL);

  func = get_function(addr);
  func->call_count += 1;
  if (accel)
    func->accel_count += 1;
  if (!func->entry_depth) {
    func->entry_start_time = now;
    func->entry_start_op = profile_opcount;
  }
  func->entry_depth += 1;

  fra = (frame_t *)glulx_malloc(sizeof(frame_t));
  if (!fra)
    fatal_error("Profiler: cannot malloc frame.");
  memset(fra, 0, sizeof(frame_t));

  fra->parent = current_frame;
  current_frame = fra;

  fra->func = func;
  fra->entry_time = now;
  fra->entry_op = profile_opcount;
  timerclear(&fra->children_time);
  fra->children_ops = 0;
}

void profile_out()
{
  frame_t *fra;
  function_t *func;
  struct timeval now, runtime;
  glui32 runops;

  if (!profiling_active)
    return;

  /* printf("### OUT\n"); */

  if (!current_frame) 
    fatal_error("Profiler: stack underflow.");

  gettimeofday(&now, NULL);

  fra = current_frame;
  func = fra->func;

  timersub(&now, &fra->entry_time, &runtime);
  runops = profile_opcount - fra->entry_op;

  timeradd(&runtime, &func->self_time, &func->self_time);
  timersub(&func->self_time, &fra->children_time, &func->self_time);
  func->self_ops += runops;
  func->self_ops -= fra->children_ops;

  if (fra->parent) {
    timeradd(&runtime, &fra->parent->children_time, &fra->parent->children_time);
    fra->parent->children_ops += runops;
  }

  if (!func->entry_depth) 
    fatal_error("Profiler: function entry underflow.");
  
  func->entry_depth -= 1;
  if (!func->entry_depth) {
    timersub(&now, &func->entry_start_time, &runtime);
    timerclear(&func->entry_start_time);

    runops = profile_opcount - func->entry_start_op;
    func->entry_start_op = 0;

    timeradd(&runtime, &func->total_time, &func->total_time);
    func->total_ops += runops;
  }

  current_frame = fra->parent;
  fra->parent = NULL;

  glulx_free(fra);
}

/* ### throw/catch */
/* ### restore/restore_undo/restart */

void profile_fail(char *reason)
{
  if (!profiling_active)
    return;

  fatal_error_2("Profiler: unable to handle operation", reason);
}

void profile_quit()
{
  int bucknum;
  function_t *func;
  char linebuf[512];
  strid_t profstr;

  if (!profiling_active)
    return;

  while (current_frame) {
    profile_out();
  }

  if (profiling_stream) {
    profstr = profiling_stream;
  }
  else if (profiling_filename) {
    frefid_t profref = glk_fileref_create_by_name(fileusage_BinaryMode|fileusage_Data, profiling_filename, 0);
    if (!profref)
        fatal_error_2("Profiler: unable to create profile output fileref", profiling_filename);

    profstr = glk_stream_open_file(profref, filemode_Write, 0);
  }
  else {
    fatal_error("Profiler: no profile output handle!");
  }

  glk_put_string_stream(profstr, "<profile>\n");

  for (bucknum=0; bucknum<FUNC_HASH_SIZE; bucknum++) {
    char total_buf[20], self_buf[20];

    for (func = functions[bucknum]; func; func=func->hash_next) {
      /* ###
      sprintf(linebuf, "function %lx called %ld times, total ops %ld, total time %s, self ops %ld,  self time %s\n",
        func->addr, func->call_count,
        func->total_ops,
        timeprint(&func->total_time, total_buf),
        func->self_ops,
        timeprint(&func->self_time, self_buf));
      ### */
      sprintf(linebuf, "  <function addr=\"%lx\" call_count=\"%ld\" accel_count=\"%ld\" total_ops=\"%ld\" total_time=\"%s\" self_ops=\"%ld\" self_time=\"%s\" />\n",
        func->addr, func->call_count, func->accel_count,
        func->total_ops,
        timeprint(&func->total_time, total_buf),
        func->self_ops,
        timeprint(&func->self_time, self_buf));
      glk_put_string_stream(profstr, linebuf);
    }
  }

  glk_put_string_stream(profstr, "</profile>\n");

  glk_stream_close(profstr, NULL);

  glulx_free(functions);
  functions = NULL;
}

#else /* VM_PROFILING */

void setup_profile(strid_t stream, char *filename)
{
    /* Profiling is not compiled in. Do nothing. */
}

int init_profile()
{
    /* Profiling is not compiled in. Do nothing. */
    return TRUE;
}

#endif /* VM_PROFILING */
