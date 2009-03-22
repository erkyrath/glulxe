/* operand.c: Glulxe code for instruction operands, reading and writing.
    Designed by Andrew Plotkin <erkyrath@netcom.com>
    http://www.eblong.com/zarf/glulx/index.html
*/

#include "glk.h"
#include "glulxe.h"
#include "opcodes.h"

/* fast_operandlist[]:
   This is a handy array in which to look up operandlists quickly.
   It stores the operandlists for the first 128 opcodes, which are
   the ones used most frequently.
*/
operandlist_t *fast_operandlist[0x80];

/* The actual immutable structures which lookup_operandlist()
   returns. */
static operandlist_t list_none = { 0, 4, NULL };
static int array_S[1] = { modeform_Store };
static operandlist_t list_S = { 1, 4, array_S };
static int array_LS[2] = { modeform_Load, modeform_Store };
static operandlist_t list_LS = { 2, 4, array_LS };
static int array_LLS[3] = { modeform_Load, modeform_Load, modeform_Store };
static operandlist_t list_LLS = { 3, 4, array_LLS };
static int array_L[1] = { modeform_Load };
static operandlist_t list_L = { 1, 4, array_L };
static int array_LL[2] = { modeform_Load, modeform_Load };
static operandlist_t list_LL = { 2, 4, array_LL };
static int array_LLL[3] = { modeform_Load, modeform_Load, modeform_Load };
static operandlist_t list_LLL = { 3, 4, array_LLL };
static operandlist_t list_2LS = { 2, 2, array_LS };
static operandlist_t list_1LS = { 2, 1, array_LS };
static int array_SL[2] = { modeform_Store, modeform_Load };
static operandlist_t list_SL = { 2, 4, array_SL };

/* init_operands():
   Set up the fast-lookup array of operandlists. This is called just
   once, when the terp starts up. 
*/
void init_operands()
{
  int ix;
  for (ix=0; ix<0x80; ix++)
    fast_operandlist[ix] = lookup_operandlist(ix);
}

/* lookup_operandlist():
   Return the operandlist for a given opcode. For opcodes in the range
   00..7F, it's faster to use the array fast_operandlist[]. 
*/
operandlist_t *lookup_operandlist(glui32 opcode)
{
  switch (opcode) {
  case op_nop: 
    return &list_none;

  case op_add:
  case op_sub:
  case op_mul:
  case op_div:
  case op_mod:
  case op_bitand:
  case op_bitor:
  case op_bitxor:
  case op_shiftl:
  case op_sshiftr:
  case op_ushiftr:
    return &list_LLS;

  case op_neg:
  case op_bitnot:
    return &list_LS;

  case op_jump:
    return &list_L;
  case op_jz:
  case op_jnz:
    return &list_LL;
  case op_jeq:
  case op_jne:
  case op_jlt:
  case op_jge:
  case op_jgt:
  case op_jle:
    return &list_LLL;

  case op_call:
    return &list_LLS;
  case op_return:
    return &list_L;
  case op_catch:
    return &list_SL;
  case op_throw:
    return &list_LL;
  case op_tailcall:
    return &list_LL;

  case op_sexb:
  case op_sexs:
    return &list_LS;

  case op_copy:
    return &list_LS;
  case op_copys:
    return &list_2LS;
  case op_copyb:
    return &list_1LS;
  case op_aload:
  case op_aloads:
  case op_aloadb:
  case op_aloadbit:
    return &list_LLS;
  case op_astore:
  case op_astores:
  case op_astoreb:
  case op_astorebit:
    return &list_LLL;

  case op_stkcount:
    return &list_S;
  case op_stkpeek:
    return &list_LS;
  case op_stkswap: 
    return &list_none;
  case op_stkroll:
    return &list_LL;
  case op_stkcopy:
    return &list_L;

  case op_streamchar:
  case op_streamnum:
  case op_streamstr:
    return &list_L;
  case op_getstringtbl:
    return &list_S;
  case op_setstringtbl:
    return &list_L;

  case op_random:
    return &list_LS;
  case op_setrandom:
    return &list_L;

  case op_verify:
    return &list_S;
  case op_restart:
    return &list_none;
  case op_save:
  case op_restore:
    return &list_LS;
  case op_saveundo:
  case op_restoreundo:
    return &list_S;
  case op_protect:
    return &list_LL;

  case op_quit:
    return &list_none;

  case op_debugtrap: 
    return &list_L;

  case op_getmemsize:
    return &list_S;
  case op_setmemsize:
    return &list_LS;

  case op_glk:
    return &list_LLS;

  default: 
    return NULL;
  }
}

/* parse_operands():
   Read the list of operands of an instruction, and put the values
   in inst. This assumes that the PC is at the beginning of the
   operand mode list (right after an opcode number.) Upon return,
   the PC will be at the beginning of the next instruction.
*/
void parse_operands(instruction_t *inst, operandlist_t *oplist)
{
  int ix;
  int numops = oplist->num_ops;
  int argsize = oplist->arg_size;
  glui32 modeaddr = pc;

  inst->desttype = 0;

  pc += (numops+1) / 2;

  for (ix=0; ix<numops; ix++) {
    int mode;
    glui32 value;
    glui32 addr = 0;

    mode = Mem1(modeaddr);
    if ((ix & 1) == 0) {
      mode = (mode & 0x0F);
    }
    else {
      mode = ((mode >> 4) & 0x0F);
      modeaddr++;
    }

    if (oplist->formlist[ix] == modeform_Load) {

      switch (mode) {

      case 8: /* pop off stack */
        if (stackptr < valstackbase+4) {
          fatal_error("Stack underflow in operand.");
        }
        stackptr -= 4;
        value = Stk4(stackptr);
        break;

      case 0: /* constant zero */
        value = 0;
        break;

      case 1: /* one-byte constant */
        /* Sign-extend from 8 bits to 32 */
        value = (glsi32)(signed char)(Mem1(pc));
        pc++;
        break;

      case 2: /* two-byte constant */
        /* Sign-extend the first byte from 8 bits to 32; the subsequent
           byte must not be sign-extended. */
        value = (glsi32)(signed char)(Mem1(pc));
        pc++;
        value = (value << 8) | (glui32)(Mem1(pc));
        pc++;
        break;

      case 3: /* four-byte constant */
        /* Bytes must not be sign-extended. */
        value = (glui32)(Mem1(pc));
        pc++;
        value = (value << 8) | (glui32)(Mem1(pc));
        pc++;
        value = (value << 8) | (glui32)(Mem1(pc));
        pc++;
        value = (value << 8) | (glui32)(Mem1(pc));
        pc++;
        break;

      /* We do a bit of clever code interweaving here. Each of these
         cases falls through to the bottom. Note that one- and two-byte
         values must not be sign-extended in these modes. */
      case 7: /* main memory, four-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 6: /* main memory, two-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 5: /* main memory, one-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* cases 5, 6, 7 all wind up here. */
        if (argsize == 4) {
          value = Mem4(addr);
        }
        else if (argsize == 2) {
          value = Mem2(addr);
        }
        else {
          value = Mem1(addr);
        }
        break;

      case 11: /* locals, four-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 10: /* locals, two-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 9: /* locals, one-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* cases 9, 10, 11 all wind up here. It's illegal for addr to not
           be four-byte aligned, but we don't check this explicitly. 
           A "strict mode" interpreter probably should. It's also illegal
           for addr to be less than zero or greater than the size of
           the locals segment. */
        addr += localsbase;
        if (argsize == 4) {
          value = Stk4(addr);
        }
        else if (argsize == 2) {
          value = Stk2(addr);
        }
        else {
          value = Stk1(addr);
        }
        break;

      case 15: /* main memory RAM, four-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 14: /* main memory RAM, two-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 13: /* main memory RAM, one-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* cases 13, 14, 15 all wind up here. */
        addr += ramstart;
        if (argsize == 4) {
          value = Mem4(addr);
        }
        else if (argsize == 2) {
          value = Mem2(addr);
        }
        else {
          value = Mem1(addr);
        }
        break;

      default:
        fatal_error("Unknown addressing mode in load operand.");
      }

      inst->value[ix] = value;

    }
    else {
      switch (mode) {

      case 0: /* discard value */
        inst->desttype = 0;
        inst->value[ix] = 0;
        break;

      case 8: /* push on stack */
        inst->desttype = 3;
        inst->value[ix] = 0;
        break;

      case 7: /* main memory, four-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 6: /* main memory, two-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 5: /* main memory, one-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* cases 5, 6, 7 all wind up here. */
        inst->desttype = 1;
        inst->value[ix] = addr;
        break;

      case 11: /* locals, four-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 10: /* locals, two-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 9: /* locals, one-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* cases 9, 10, 11 all wind up here. It's illegal for addr to not
           be four-byte aligned, but we don't check this explicitly. 
           A "strict mode" interpreter probably should. It's also illegal
           for addr to be less than zero or greater than the size of
           the locals segment. */
        inst->desttype = 2;
        /* We don't add localsbase here; the store address for desttype 2
           is relative to the current locals segment, not an absolute
           stack position. */
        inst->value[ix] = addr;
        break;

      case 15: /* main memory RAM, four-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 14: /* main memory RAM, two-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* no break */
      case 13: /* main memory RAM, one-byte address */
        addr = (addr << 8) | (glui32)(Mem1(pc));
        pc++;
        /* cases 13, 14, 15 all wind up here. */
        inst->desttype = 1;
        addr += ramstart;
        inst->value[ix] = addr;
        break;

      case 1:
      case 2:
      case 3:
        fatal_error("Constant addressing mode in store operand.");

      default:
        fatal_error("Unknown addressing mode in store operand.");
      }
    }
  }
}

/* store_operand():
   Store a result value, according to the desttype and destaddress given.
   This is usually used to store the result of an opcode, but it's also
   used by any code that pulls a call-stub off the stack.
*/
void store_operand(glui32 desttype, glui32 destaddr, glui32 storeval)
{
  switch (desttype) {

  case 0: /* do nothing; discard the value. */
    return;

  case 1: /* main memory. */
    MemW4(destaddr, storeval);
    return;

  case 2: /* locals. */
    destaddr += localsbase;
    StkW4(destaddr, storeval);
    return;

  case 3: /* push on stack. */
    if (stackptr+4 > stacksize) {
      fatal_error("Stack overflow in store operand.");
    }
    StkW4(stackptr, storeval);
    stackptr += 4;
    return;

  default:
    fatal_error("Unknown destination type in store operand.");

  }
}

void store_operand_s(glui32 desttype, glui32 destaddr, glui32 storeval)
{
  storeval &= 0xFFFF;

  switch (desttype) {

  case 0: /* do nothing; discard the value. */
    return;

  case 1: /* main memory. */
    MemW2(destaddr, storeval);
    return;

  case 2: /* locals. */
    destaddr += localsbase;
    StkW2(destaddr, storeval);
    return;

  case 3: /* push on stack. A four-byte value is actually pushed. */
    if (stackptr+4 > stacksize) {
      fatal_error("Stack overflow in store operand.");
    }
    StkW4(stackptr, storeval);
    stackptr += 4;
    return;

  default:
    fatal_error("Unknown destination type in store operand.");

  }
}

void store_operand_b(glui32 desttype, glui32 destaddr, glui32 storeval)
{
  storeval &= 0xFF;

  switch (desttype) {

  case 0: /* do nothing; discard the value. */
    return;

  case 1: /* main memory. */
    MemW1(destaddr, storeval);
    return;

  case 2: /* locals. */
    destaddr += localsbase;
    StkW1(destaddr, storeval);
    return;

  case 3: /* push on stack. A four-byte value is actually pushed. */
    if (stackptr+4 > stacksize) {
      fatal_error("Stack overflow in store operand.");
    }
    StkW4(stackptr, storeval);
    stackptr += 4;
    return;

  default:
    fatal_error("Unknown destination type in store operand.");

  }
}
