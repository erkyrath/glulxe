/* string.c: Glulxe string and text functions.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"

#define CACHEBITS (4)
#define CACHESIZE (1<<CACHEBITS) 
#define CACHEMASK (15)

typedef struct cacheblock_struct {
  int depth; /* 1 to 4 */
  int type;
  union {
    struct cacheblock_struct *branches;
    unsigned char ch;
    glui32 addr;
  } u;
} cacheblock_t;

static int never_cache_stringtable = FALSE;

/* The current string-decoding tables, broken out into a fast and
   easy-to-use form. */
static int tablecache_valid = FALSE;
static cacheblock_t tablecache;

static void dropcache(cacheblock_t *cablist);
static void buildcache(cacheblock_t *cablist, glui32 nodeaddr, int depth,
  int mask);
static void dumpcache(cacheblock_t *cablist, int count, int indent);

/* stream_num():
   Write a signed integer to the current output stream.
*/
void stream_num(glsi32 val)
{
  char buf[16];
  glui32 ival;
  int ix;

  if (val == 0) {
    glk_put_char('0');
    return;
  }

  if (val < 0) {
    glk_put_char('-');
    ival = -val;
  }
  else {
    ival = val;
  }

  ix = 0;
  while (ival != 0) {
    buf[ix] = (ival % 10) + '0';
    ix++;
    ival /= 10;
  }

  while (ix) {
    ix--;
    glk_put_char(buf[ix]);
  }
}

/* stream_hexnum():
   Write a signed integer to the current output stream.
*/
void stream_hexnum(glsi32 val)
{
  char buf[16];
  glui32 ival;
  int ix;

  if (val == 0) {
    glk_put_char('0');
    return;
  }

  if (val < 0) {
    glk_put_char('-');
    ival = -val;
  }
  else {
    ival = val;
  }

  ix = 0;
  while (ival != 0) {
    buf[ix] = (ival % 16) + '0';
    if (buf[ix] > '9')
      buf[ix] += ('A' - ('9' + 1));
    ix++;
    ival /= 16;
  }

  while (ix) {
    ix--;
    glk_put_char(buf[ix]);
  }
}

/* stream_string():
   Write a Glulx string object to the current output stream.
*/
void stream_string(glui32 addr, int inmiddle, int bitnum)
{
  int ch;
  int type;
  int alldone = FALSE;
  int substring = inmiddle;

  if (!addr)
    fatal_error("Called stream_string with null address.");
  
  while (!alldone) {

    if (!inmiddle) {
      type = Mem1(addr);
      addr++;
      bitnum = 0;
    }
    else {
      type = 0xE1;
    }

    if (type == 0xE1) {
      if (tablecache_valid) {
	int bits, numbits;
	int readahead;
	glui32 tmpaddr;
	cacheblock_t *cablist;
	int done = 0;

	/* bitnum is already set right */
	bits = Mem1(addr); 
	if (bitnum)
	  bits >>= bitnum;
	numbits = (8 - bitnum);
	readahead = FALSE;

	if (tablecache.type != 0) {
	  /* This is a bit of a cheat. If the top-level block is not
	     a branch, then it must be a string-terminator -- otherwise
	     the string would be an infinite repetition of that block.
	     We check for this case and bail immediately. */
	  done = 1;
	}

	cablist = tablecache.u.branches;
	while (!done) {
	  cacheblock_t *cab;

	  if (numbits < CACHEBITS) {
	    /* readahead is certainly false */
	    int newbyte = Mem1(addr+1);
	    bits |= (newbyte << numbits);
	    numbits += 8;
	    readahead = TRUE;
	  }

	  cab = &(cablist[bits & CACHEMASK]);
	  numbits -= cab->depth;
	  bits >>= cab->depth;
	  bitnum += cab->depth;
	  if (bitnum >= 8) {
	    addr += 1;
	    bitnum -= 8;
	    if (readahead) {
	      readahead = FALSE;
	    }
	    else {
	      int newbyte = Mem1(addr);
	      bits |= (newbyte << numbits);
	      numbits += 8;
	    }
	  }

	  switch (cab->type) {
	  case 0x00: /* non-leaf node */
	    cablist = cab->u.branches;
	    break;
	  case 0x01: /* string terminator */
	    done = 1;
	    break;
	  case 0x02: /* single character */
	    glk_put_char(cab->u.ch);
	    cablist = tablecache.u.branches;
	    break;
	  case 0x03: /* C string */
	    for (tmpaddr=cab->u.addr; (ch=Mem1(tmpaddr)) != '\0'; tmpaddr++) 
	      glk_put_char(ch);
	    cablist = tablecache.u.branches; 
	    break;
	  case 0x08:
	  case 0x09:
	  case 0x0A:
	  case 0x0B: 
	    {
	      glui32 oaddr;
	      int otype;
	      oaddr = cab->u.addr;
	      if (cab->type >= 0x09)
		oaddr = Mem4(oaddr);
	      if (cab->type == 0x0B)
		oaddr = Mem4(oaddr);
	      otype = Mem1(oaddr);
	      if (!substring) {
		push_callstub(0x11, 0);
		substring = TRUE;
	      }
	      if (otype >= 0xE0 && otype <= 0xFF) {
		pc = addr;
		push_callstub(0x10, bitnum);
		inmiddle = FALSE;
		addr = oaddr;
		done = 2;
	      }
	      else if (otype >= 0xC0 && otype <= 0xDF) {
		glui32 argc;
		glui32 *argv;
		if (cab->type == 0x0A || cab->type == 0x0B) {
		  argc = Mem4(cab->u.addr+4);
		  argv = pop_arguments(argc, cab->u.addr+8);
		}
		else {
		  argc = 0;
		  argv = NULL;
		}
		pc = addr;
		push_callstub(0x10, bitnum);
		enter_function(oaddr, argc, argv);
		return;
	      }
	      else {
		fatal_error("Unknown object while decoding string indirect reference.");
	      }
	    }
	    break;
	  default:
	    fatal_error("Unknown entity in string decoding (cached).");
	    break;
	  }
	}
	if (done > 1) {
	  continue; /* restart the top-level loop */
	}
      }
      else {
	glui32 node;
	int byte;
	int nodetype;
	int done = 0;

	if (!stringtable)
	  fatal_error("Attempted to print a compressed string with no table set.");
	/* bitnum is already set right */
	byte = Mem1(addr); 
	if (bitnum)
	  byte >>= bitnum;
	node = Mem4(stringtable+8);
	while (!done) {
	  nodetype = Mem1(node);
	  node++;
	  switch (nodetype) {
	  case 0x00: /* non-leaf node */
	    if (byte & 1) 
	      node = Mem4(node+4);
	    else
	      node = Mem4(node+0);
	    if (bitnum == 7) {
	      bitnum = 0;
	      addr++;
	      byte = Mem1(addr);
	    }
	    else {
	      bitnum++;
	      byte >>= 1;
	    }
	    break;
	  case 0x01: /* string terminator */
	    done = 1;
	    break;
	  case 0x02: /* single character */
	    ch = Mem1(node);
	    glk_put_char(ch);
	    node = Mem4(stringtable+8);
	    break;
	  case 0x03: /* C string */
	    for (; (ch=Mem1(node)) != '\0'; node++) 
	      glk_put_char(ch);
	    node = Mem4(stringtable+8);
	    break;
	  case 0x08:
	  case 0x09:
	  case 0x0A:
	  case 0x0B: 
	    {
	      glui32 oaddr;
	      int otype;
	      oaddr = Mem4(node);
	      if (nodetype == 0x09 || nodetype == 0x0B)
		oaddr = Mem4(oaddr);
	      otype = Mem1(oaddr);
	      if (!substring) {
		push_callstub(0x11, 0);
		substring = TRUE;
	      }
	      if (otype >= 0xE0 && otype <= 0xFF) {
		pc = addr;
		push_callstub(0x10, bitnum);
		inmiddle = FALSE;
		addr = oaddr;
		done = 2;
	      }
	      else if (otype >= 0xC0 && otype <= 0xDF) {
		glui32 argc;
		glui32 *argv;
		if (nodetype == 0x0A || nodetype == 0x0B) {
		  argc = Mem4(node+4);
		  argv = pop_arguments(argc, node+8);
		}
		else {
		  argc = 0;
		  argv = NULL;
		}
		pc = addr;
		push_callstub(0x10, bitnum);
		enter_function(oaddr, argc, argv);
		return;
	      }
	      else {
		fatal_error("Unknown object while decoding string indirect reference.");
	      }
	    }
	    break;
	  default:
	    fatal_error("Unknown entity in string decoding.");
	    break;
	  }
	}
	if (done > 1) {
	  continue; /* restart the top-level loop */
	}
      }
    }
    else if (type == 0xE0) {
      while (1) {
	ch = Mem1(addr);
	addr++;
	if (ch == '\0')
	  break;
	glk_put_char(ch);
      }
    }
    else if (type >= 0xE0 && type <= 0xFF) {
      fatal_error("Attempt to print unknown type of string.");
    }
    else {
      fatal_error("Attempt to print non-string.");
    }

    if (!substring) {
      /* Just get straight out. */
      alldone = TRUE;
    }
    else {
      /* Pop a stub and see what's to be done. */
      addr = pop_callstub_string(&bitnum);
      if (addr == 0) {
	alldone = TRUE;
      }
      else {
	inmiddle = TRUE;
      }
    }
  }
}

/* stream_get_table():
   Get the current table address. 
*/
glui32 stream_get_table()
{
  return stringtable;
}

/* stream_set_table():
   Set the current table address, and rebuild decoding cache. 
*/
void stream_set_table(glui32 addr)
{
  if (stringtable == addr)
    return;

  /* Drop cache. */
  if (tablecache_valid) {
    if (tablecache.type == 0)
      dropcache(tablecache.u.branches);
    tablecache.u.branches = NULL;
    tablecache_valid = FALSE;
  }

  stringtable = addr;

  if (stringtable) {
    /* Build cache. We can only do this if the table is entirely in ROM. */
    glui32 tablelen = Mem4(stringtable);
    glui32 rootaddr = Mem4(stringtable+8);
    if (stringtable+tablelen <= ramstart && !never_cache_stringtable) {
      buildcache(&tablecache, rootaddr, CACHEBITS, 0);
      /* dumpcache(&tablecache, 1, 0); */
      tablecache_valid = TRUE;
    }
  }
}

static void buildcache(cacheblock_t *cablist, glui32 nodeaddr, int depth,
  int mask)
{
  int ix, type;

  type = Mem1(nodeaddr);

  if (type == 0 && depth == CACHEBITS) {
    cacheblock_t *list, *cab;
    list = (cacheblock_t *)glulx_malloc(sizeof(cacheblock_t) * CACHESIZE);
    buildcache(list, nodeaddr, 0, 0);
    cab = &(cablist[mask]);
    cab->type = 0;
    cab->depth = CACHEBITS;
    cab->u.branches = list;
    return;
  }

  if (type == 0) {
    glui32 leftaddr  = Mem4(nodeaddr+1);
    glui32 rightaddr = Mem4(nodeaddr+5);
    buildcache(cablist, leftaddr, depth+1, mask);
    buildcache(cablist, rightaddr, depth+1, (mask | (1 << depth)));
    return;
  }

  /* Leaf node. */
  nodeaddr++;
  for (ix = mask; ix < CACHESIZE; ix += (1 << depth)) {
    cacheblock_t *cab = &(cablist[ix]);
    cab->type = type;
    cab->depth = depth;
    switch (type) {
    case 0x02:
      cab->u.ch = Mem1(nodeaddr);
      break;
    case 0x03:
    case 0x0A:
    case 0x0B:
      cab->u.addr = nodeaddr;
      break;
    case 0x08:
    case 0x09:
      cab->u.addr = Mem4(nodeaddr);
      break;
    }
  }
}

#if 0
#include <stdio.h>
static void dumpcache(cacheblock_t *cablist, int count, int indent)
{
  int ix, jx;

  for (ix=0; ix<count; ix++) {
    cacheblock_t *cab = &(cablist[ix]); 
    for (jx=0; jx<indent; jx++)
      printf("  ");
    printf("%X: ", ix);
    switch (cab->type) {
    case 0:
      printf("...\n");
      dumpcache(cab->u.branches, CACHESIZE, indent+1);
      break;
    case 1:
      printf("<EOS>\n");
      break;
    case 2:
      printf("0x%02X", cab->u.ch);
      if (cab->u.ch < 32)
	printf(" ''\n");
      else
	printf(" '%c'\n", cab->u.ch);
      break;
    default:
      printf("type %02X, address %06lX\n", cab->type, cab->u.addr);
      break;
    }
  }
}
#endif /* 0 */

static void dropcache(cacheblock_t *cablist)
{
  int ix;
  for (ix=0; ix<CACHESIZE; ix++) {
    cacheblock_t *cab = &(cablist[ix]);
    if (cab->type == 0) {
      dropcache(cab->u.branches);
      cab->u.branches = NULL;
    }
  }
  glulx_free(cablist);
}

char *make_temp_string(glui32 addr)
{
  return "###string args not yet implemented###";
}

void free_temp_string(char *str)
{

}
