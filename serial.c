/* serial.c: Glulxe code for saving and restoring the VM state.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include <string.h>
#include "glk.h"
#include "glulxe.h"

/* This structure allows us to write either to a Glk stream or to
   a dynamically-allocated memory chunk. */
typedef struct dest_struct {
  int ismem;
  
  /* If it's a Glk stream: */
  strid_t str;

  /* If it's a block of memory: */
  unsigned char *ptr;
  glui32 pos;
  glui32 size;
} dest_t;

/* This can be adjusted before startup by platform-specific startup
   code -- that is, preference code. */
int max_undo_level = 8;

static int undo_chain_size = 0;
static int undo_chain_num = 0;
unsigned char **undo_chain = NULL;

static glui32 protect_pos = 0;
static glui32 protect_len = 0;

static glui32 write_memstate(dest_t *dest);
static glui32 write_stackstate(dest_t *dest, int portable);
static glui32 read_memstate(dest_t *dest, glui32 chunklen);
static glui32 read_stackstate(dest_t *dest, glui32 chunklen, int portable);
static int write_long(dest_t *dest, glui32 val);
static int read_long(dest_t *dest, glui32 *val);
static int reposition_write(dest_t *dest, glui32 pos);

/* init_serial():
   Set up the undo chain and anything else that needs to be set up.
*/
int init_serial()
{
  undo_chain_num = 0;
  undo_chain_size = max_undo_level;
  undo_chain = (unsigned char **)glulx_malloc(sizeof(unsigned char *) * undo_chain_size);
  if (!undo_chain)
    return FALSE;

  return TRUE;
}

/* perform_saveundo():
   Add a state pointer to the undo chain. This returns 0 on success,
   1 on failure.
*/
glui32 perform_saveundo()
{
  dest_t dest;
  glui32 res;
  glui32 memstart, memlen, stackstart, stacklen;

  /* The format for undo-saves is simpler than for saves on disk. We
     just have a memory chunk followed by a stack chunk, and we skip
     the IFF chunk headers (although the size fields are still there.)
     We also don't bother with IFF's 16-bit alignment. */

  if (undo_chain_size == 0)
    return 1;

  dest.ismem = TRUE;
  dest.size = 0;
  dest.pos = 0;
  dest.ptr = NULL;
  dest.str = NULL;

  res = 0;
  if (res == 0) {
    res = write_long(&dest, 0); /* space for chunk length */
  }
  if (res == 0) {
    memstart = dest.pos;
    res = write_memstate(&dest);
    memlen = dest.pos - memstart;
  }
  if (res == 0) {
    res = write_long(&dest, 0); /* space for chunk length */
  }
  if (res == 0) {
    stackstart = dest.pos;
    res = write_stackstate(&dest, FALSE);
    stacklen = dest.pos - stackstart;
  }
  if (res == 0) {
    /* Trim it down to the perfect size. */
    dest.ptr = glulx_realloc(dest.ptr, dest.pos);
    if (!dest.ptr)
      res = 1;
  }
  if (res == 0) {
    res = reposition_write(&dest, memstart-4);
  }
  if (res == 0) {
    res = write_long(&dest, memlen);
  }
  if (res == 0) {
    res = reposition_write(&dest, stackstart-4);
  }
  if (res == 0) {
    res = write_long(&dest, stacklen);
  }

  if (res == 0) {
    /* It worked. */
    if (undo_chain_size > 1)
      memmove(undo_chain+1, undo_chain, 
	(undo_chain_size-1) * sizeof(unsigned char *));
    undo_chain[0] = dest.ptr;
    if (undo_chain_num < undo_chain_size)
      undo_chain_num += 1;
    dest.ptr = NULL;
  }
  else {
    /* It didn't work. */
    if (dest.ptr) {
      glulx_free(dest.ptr);
      dest.ptr = NULL;
    }
  }
    
  return res;
}

/* perform_restoreundo():
   Pull a state pointer from the undo chain. This returns 0 on success,
   1 on failure. Note that if it succeeds, the frameptr, localsbase,
   and valstackbase registers are invalid; they must be rebuilt from
   the stack.
*/
glui32 perform_restoreundo()
{
  dest_t dest;
  glui32 res, val;

  if (undo_chain_size == 0 || undo_chain_num == 0)
    return 1;

  dest.ismem = TRUE;
  dest.size = 0;
  dest.pos = 0;
  dest.ptr = undo_chain[0];
  dest.str = NULL;

  res = 0;
  if (res == 0) {
    res = read_long(&dest, &val);
  }
  if (res == 0) {
    res = read_memstate(&dest, val);
  }
  if (res == 0) {
    res = read_long(&dest, &val);
  }
  if (res == 0) {
    res = read_stackstate(&dest, val, FALSE);
  }
  /* ### really, many of the failure modes of those two calls ought to
     cause fatal errors. The stack or main memory may be damaged now. */

  if (res == 0) {
    /* It worked. */
    if (undo_chain_size > 1)
      memmove(undo_chain, undo_chain+1,
        (undo_chain_size-1) * sizeof(unsigned char *));
    undo_chain_num -= 1;
    glulx_free(dest.ptr);
    dest.ptr = NULL;
  }
  else {
    /* It didn't work. */
    dest.ptr = NULL;
  }

  return res;
}

static int reposition_write(dest_t *dest, glui32 pos)
{
  if (dest->ismem) {
    dest->pos = pos;
  }
  else {
    glk_stream_set_position(dest->str, pos, seekmode_Start);
    dest->pos = pos;
  }

  return 0;
}

static int write_buffer(dest_t *dest, unsigned char *ptr, glui32 len)
{
  if (dest->ismem) {
    if (dest->pos+len > dest->size) {
      dest->size = dest->pos+len+1024;
      if (!dest->ptr) {
	dest->ptr = glulx_malloc(dest->size);
      }
      else {
	dest->ptr = glulx_realloc(dest->ptr, dest->size);
      }
      if (!dest->ptr)
	return 1;
    }
    memcpy(dest->ptr+dest->pos, ptr, len);
  }
  else {
    glk_put_buffer_stream(dest->str, ptr, len);
  }

  dest->pos += len;

  return 0;
}

static int read_buffer(dest_t *dest, unsigned char *ptr, glui32 len)
{
  glui32 newlen;

  if (dest->ismem) {
    /*###if (dest->pos+len > dest->size) 
      return 1;*/
    memcpy(ptr, dest->ptr+dest->pos, len);
  }
  else {
    newlen = glk_get_buffer_stream(dest->str, ptr, len);
    if (newlen != len)
      return 1;
  }

  dest->pos += len;

  return 0;
}

static int write_long(dest_t *dest, glui32 val)
{
  unsigned char buf[4];
  Write4(buf, val);
  return write_buffer(dest, buf, 4);
}

static int write_short(dest_t *dest, glui16 val)
{
  unsigned char buf[2];
  Write2(buf, val);
  return write_buffer(dest, buf, 2);
}

static int write_byte(dest_t *dest, unsigned char val)
{
  return write_buffer(dest, &val, 1);
}

static int read_long(dest_t *dest, glui32 *val)
{
  unsigned char buf[4];
  int res = read_buffer(dest, buf, 4);
  if (res)
    return res;
  *val = Read4(buf);
  return 0;
}

static int read_short(dest_t *dest, glui16 *val)
{
  unsigned char buf[2];
  int res = read_buffer(dest, buf, 2);
  if (res)
    return res;
  *val = Read2(buf);
  return 0;
}

static int read_byte(dest_t *dest, unsigned char *val)
{
  return read_buffer(dest, val, 1);
}

static glui32 write_memstate(dest_t *dest)
{
  glui32 res, pos;
  int val;
  int runlen;
  unsigned char ch;

  res = write_long(dest, endmem);
  if (res)
    return res;

  runlen = 0;
  glk_stream_set_position(gamefile, ramstart, seekmode_Start);

  for (pos=ramstart; pos<endmem; pos++) {
    ch = Mem1(pos);
    if (pos < endgamefile) {
      val = glk_get_char_stream(gamefile);
      if (val == -1) {
	fatal_error("The game file ended unexpectedly while saving.");
      }
      ch ^= (unsigned char)val;
    }
    if (ch == 0) {
      runlen++;
    }
    else {
      /* Write any run we've got. */
      while (runlen) {
	if (runlen >= 0x100)
	  val = 0x100;
	else
	  val = runlen;
	res = write_byte(dest, 0);
	if (res)
	  return res;
	res = write_byte(dest, (val-1));
	if (res)
	  return res;
	runlen -= val;
      }
      /* Write the byte we got. */
      res = write_byte(dest, ch);
      if (res)
	return res;
    }
  }
  /* It's possible we've got a run left over, but we don't write it. */

  return 0;
}

static glui32 read_memstate(dest_t *dest, glui32 chunklen)
{
  glui32 chunkend = dest->pos + chunklen;
  glui32 newlen;
  glui32 res, pos;
  int val;
  int runlen;
  unsigned char ch, ch2;

  res = read_long(dest, &newlen);
  if (res)
    return res;

  res = change_memsize(newlen);
  if (res)
    return res;

  runlen = 0;
  glk_stream_set_position(gamefile, ramstart, seekmode_Start);

  for (pos=ramstart; pos<endmem; pos++) {
    if (pos < endgamefile) {
      val = glk_get_char_stream(gamefile);
      if (val == -1) {
	fatal_error("The game file ended unexpectedly while restoring.");
      }
      ch = (unsigned char)val;
    }
    else {
      ch = 0;
    }

    if (dest->pos >= chunkend) {
      /* we're into the final, unstored run. */
    }
    else if (runlen) {
      runlen--;
    }
    else {
      res = read_byte(dest, &ch2);
      if (res)
	return res;
      if (ch2 == 0) {
	res = read_byte(dest, &ch2);
	if (res)
	  return res;
	runlen = (glui32)ch2;
      }
      else {
	ch ^= ch2;
      }
    }

    MemW1(pos, ch);
  }

  return 0;
}

static glui32 write_stackstate(dest_t *dest, int portable)
{
  glui32 res, pos;
  int val;
  unsigned char ch;

  /* If we're storing for the purpose of undo, we don't need to do any
     byte-swapping, because the result will only be used by this session. */
  if (!portable) {
    res = write_buffer(dest, stack, stackptr);
    if (res)
      return res;
    return 0;
  }

  /* Write a portable stack image. */

  return 1;
}

static glui32 read_stackstate(dest_t *dest, glui32 chunklen, int portable)
{
  glui32 res, pos;
  unsigned char ch;

  if (chunklen > stacksize)
    return 1;

  stackptr = chunklen;
  frameptr = 0;
  valstackbase = 0;
  localsbase = 0;

  if (!portable) {
    res = read_buffer(dest, stack, stackptr);
    if (res)
      return res;
    return 0;
  }

  return 1;
}

