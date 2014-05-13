/* debugger.c: Glulxe debugger functions.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

/* Don't get excited. This is the bare rudiments of a source-level debugger.
   I mostly want to feel out how the API works. (It has to plug into the
   Glk library in a general way.)

   The big hole right now: we want to pause the game and dig around in the
   state, including "up" and "down" the stack. But we can't save information
   about the state between commands. Do we use cycle_handler to inform us
   when the VM is running? Is that an adequate guarantee that the stack
   won't change?

   We also want the VM to be able to jump into "debug forever" mode
   (on crash or @debugtrap). Then Glk should just send debug commands
   until we decide otherwise? Or until the user-via-Glk decides otherwise?
*/

#include "glk.h"
#include "glulxe.h"

#if VM_DEBUGGER

#include <string.h>
#include <sys/time.h>
#include "gi_debug.h" 
#include <libxml/xmlreader.h>

/* Data structures to store the debug info in memory. */

typedef enum grouptype_enum {
    grp_None = 0,
    grp_Constant = 1,
    grp_Routine = 2,
    grp_Global = 3,
    grp_Object = 4,
    grp_Array = 5,
} grouptype;

/* Used for constants, globals, locals, objects -- the meaning of the value
   field varies. */
typedef struct infoconstant_struct {
    const xmlChar *identifier;
    int32_t value;
} infoconstant;

typedef struct inforoutine_struct {
    const xmlChar *identifier;
    int32_t address;
    int32_t length;

    /* Address of the next higher function. May be beyond length if there
       are gaps. */
    int32_t nextaddress; 

    /* The locals is a block of infoconstants where the value is 
       frame-offset. We adopt Inform's assumption that locals are
       always 4 bytes long. */
    int32_t numlocals;
    infoconstant *locals;
} inforoutine;

typedef struct infoarray_struct {
    const xmlChar *identifier;
    int32_t address;
    /* should have byte length, element size, starts-with-length */
} infoarray;

typedef struct debuginfofile_struct {
    strid_t str;
    int32_t strread;
    int32_t strreadmax;
    int failed;

    grouptype curgrouptype;

    int tempcounter;
    infoconstant *tempconstant;
    inforoutine *temproutine;
    infoarray *temparray;
    int tempnumlocals;
    int templocalssize;
    infoconstant *templocals;

    const xmlChar *storyfileprefix;
    xmlHashTablePtr constants;
    xmlHashTablePtr globals;
    xmlHashTablePtr objects;
    xmlHashTablePtr arrays;
    xmlHashTablePtr routines;
    int numroutines;
    inforoutine **routinelist; /* array, ordered by address */
} debuginfofile;

/* This global holds the loaded debug info, if we have any. */
static debuginfofile *debuginfo = NULL;

/* Internal functions used while loading the debug info. */

static int xmlreadstreamfunc(void *rock, char *buffer, int len);
static int xmlreadchunkfunc(void *rock, char *buffer, int len);
static int xmlclosefunc(void *rock);
static void xmlhandlenode(xmlTextReaderPtr reader, debuginfofile *context);
static int finalize_debuginfo(debuginfofile *context);

static debuginfofile *create_debuginfofile()
{
    debuginfofile *context = (debuginfofile *)malloc(sizeof(debuginfofile));
    context->str = NULL;
    context->failed = 0;
    context->tempconstant = NULL;
    context->temparray = NULL;
    context->temproutine = NULL;
    context->tempnumlocals = 0;
    context->templocals = NULL;
    context->templocalssize = 0;
    context->curgrouptype = grp_None;
    context->constants = xmlHashCreate(16);
    context->globals = xmlHashCreate(16);
    context->objects = xmlHashCreate(16);
    context->arrays = xmlHashCreate(16);
    context->routines = xmlHashCreate(16);
    context->numroutines = 0;
    context->routinelist = NULL;
    context->storyfileprefix = NULL;

    return context;
}

static void free_debuginfofile(debuginfofile *context)
{
    if (!context)
        return;

    context->str = NULL;
    context->tempconstant = NULL;
    context->temparray = NULL;
    context->temproutine = NULL;
 
    /* We don't bother to free the member structures, because this
       only happens once at startup and then only on error
       conditions. */

    if (context->constants) {
        xmlHashFree(context->constants, NULL);
        context->constants = NULL;
    }

    if (context->globals) {
        xmlHashFree(context->globals, NULL);
        context->globals = NULL;
    }

    if (context->objects) {
        xmlHashFree(context->objects, NULL);
        context->objects = NULL;
    }

    if (context->arrays) {
        xmlHashFree(context->arrays, NULL);
        context->arrays = NULL;
    }

    if (context->routines) {
        xmlHashFree(context->routines, NULL);
        context->routines = NULL;
    }

    if (context->routinelist) {
        free(context->routinelist);
        context->routinelist = NULL;
    }

    free(context);
}

static infoconstant *create_infoconstant()
{
    infoconstant *cons = (infoconstant *)malloc(sizeof(infoconstant));
    cons->identifier = NULL;
    cons->value = 0;
    return cons;
}

static infoarray *create_infoarray()
{
    infoarray *cons = (infoarray *)malloc(sizeof(infoarray));
    cons->identifier = NULL;
    cons->address = 0;
    /* more fields */
    return cons;
}

static inforoutine *create_inforoutine()
{
    inforoutine *cons = (inforoutine *)malloc(sizeof(inforoutine));
    cons->identifier = NULL;
    cons->address = 0;
    cons->length = 0;
    cons->nextaddress = 0;
    cons->numlocals = 0;
    cons->locals = NULL;
    return cons;
}

static void add_routine_to_table(void *obj, void *rock, xmlChar *name)
{
    debuginfofile *context = rock;
    inforoutine *routine = obj;

    if (context->tempcounter >= context->numroutines) {
        printf("### array overflow!\n"); /*###*/
        return;
    }

    context->routinelist[context->tempcounter++] = routine;
}

static int sort_routines_table(const void *obj1, const void *obj2)
{
    inforoutine **routine1 = (inforoutine **)obj1;
    inforoutine **routine2 = (inforoutine **)obj2;

    return ((*routine1)->address - (*routine2)->address);
}

static int find_routine_in_table(const void *keyptr, const void *obj)
{
    /* Binary-search callback. We rely on address and nextaddress so
       that there are no gaps. */

    glui32 addr = *(glui32 *)(keyptr);
    inforoutine **routine = (inforoutine **)obj;

    if (addr < (*routine)->address)
        return -1;
    if (addr >= (*routine)->nextaddress)
        return 1;
    return 0;
}

/* Top-level function for loading debug info from a Glk stream.
   The debug data must take up the entire file; this will read until EOF.
   If successful, fills out the debuginfo global and returns true.
   If not, reports an error and returns false.
   (The stream will not be closed.)
 */
int debugger_load_info_stream(strid_t stream)
{
    debuginfofile *context = create_debuginfofile();
    context->str = stream;
    context->strread = 0; /* not used */
    context->strreadmax = 0; /* not used */

    xmlTextReaderPtr reader;
    reader = xmlReaderForIO(xmlreadstreamfunc, xmlclosefunc, context, 
        NULL, NULL, 
        XML_PARSE_RECOVER|XML_PARSE_NOENT|XML_PARSE_NONET|XML_PARSE_NOCDATA|XML_PARSE_COMPACT);
    if (!reader) {
        printf("Error: Unable to create XML reader.\n"); /*###*/
        free_debuginfofile(context);
        return 0;
    }

    while (1) {
        int res = xmlTextReaderRead(reader);
        if (res < 0) {
            context->failed = 1;
            break; /* error */
        }
        if (res == 0) {
            break; /* EOF */
        }
        xmlhandlenode(reader, context);
    }

    xmlFreeTextReader(reader);
    context->str = NULL; /* the reader didn't close it, but we're done with it. */

    if (context->failed) {
        printf("Error: Unable to load debug info.\n"); /*###*/
        free_debuginfofile(context);
        return 0;
    }

    /* Now that all the data is loaded in, we go through and create some
       indexes that will be handy. */
    return finalize_debuginfo(context);
}

/* Top-level function for loading debug info from a segment of a Glk stream.
   This starts at position pos in the file and reads len bytes.
   If successful, fills out the debuginfo global and returns true.
   If not, reports an error and returns false.
   (The stream will not be closed.)
 */
int debugger_load_info_chunk(strid_t stream, glui32 pos, glui32 len)
{
    debuginfofile *context = create_debuginfofile();
    context->str = stream;
    context->strread = 0;
    context->strreadmax = len;

    glk_stream_set_position(stream, pos, seekmode_Start);

    xmlTextReaderPtr reader;
    reader = xmlReaderForIO(xmlreadchunkfunc, xmlclosefunc, context, 
        NULL, NULL, 
        XML_PARSE_RECOVER|XML_PARSE_NOENT|XML_PARSE_NONET|XML_PARSE_NOCDATA|XML_PARSE_COMPACT);
    if (!reader) {
        printf("Error: Unable to create XML reader.\n"); /*###*/
        free_debuginfofile(context);
        return 0;
    }

    while (1) {
        int res = xmlTextReaderRead(reader);
        if (res < 0) {
            context->failed = 1;
            break; /* error */
        }
        if (res == 0) {
            break; /* EOF */
        }
        xmlhandlenode(reader, context);
    }

    xmlFreeTextReader(reader);
    context->str = NULL; /* the reader didn't close it, but we're done with it. */

    if (context->failed) {
        printf("Error: Unable to load debug info.\n"); /*###*/
        free_debuginfofile(context);
        return 0;
    }

    /* Now that all the data is loaded in, we go through and create some
       indexes that will be handy. */
    return finalize_debuginfo(context);
}

/* xmlReader callback to read from a stream (until EOF). */
static int xmlreadstreamfunc(void *rock, char *buffer, int len)
{
    debuginfofile *context = rock;
    int res = glk_get_buffer_stream(context->str, buffer, len);
    if (res < 0)
        return -1;
    return res;
}

/* xmlReader callback to read from a stream (until a given position). */
static int xmlreadchunkfunc(void *rock, char *buffer, int len)
{
    debuginfofile *context = rock;
    if (context->strread >= context->strreadmax)
        return 0;

    if (len > context->strreadmax - context->strread)
        len = context->strreadmax - context->strread;
    int res = glk_get_buffer_stream(context->str, buffer, len);
    if (res < 0)
        return -1;
    context->strread += res;
    return res;
}

/* xmlReader callback to stop reading a stream. We don't actually close
   the stream here, just discard the reference. */
static int xmlclosefunc(void *rock)
{
    debuginfofile *context = rock;
    context->str = NULL;
    return 0;
}

/* xmlReader callback: an XML node has arrived. (Might be the beginning of
   a tag, the end of a tag, or a block of text.) 

   All the work of parsing the debug format happens here, which is why this
   function is big and ugly.
*/
static void xmlhandlenode(xmlTextReaderPtr reader, debuginfofile *context)
{
    int depth = xmlTextReaderDepth(reader);
    int nodetype = xmlTextReaderNodeType(reader);

    if (depth == 0) {
        if (nodetype == XML_ELEMENT_NODE) {
            const xmlChar *name = xmlTextReaderConstName(reader);
            if (xmlStrcmp(name, BAD_CAST "inform-story-file")) {
                printf("Error: This is not an Inform debug info file.\n"); /*###*/
                context->failed = 1;
            }
        }
        else if (nodetype == XML_ELEMENT_DECL) {
            /* End of document */
            context->curgrouptype = grp_None;
            context->tempconstant = NULL;
            context->temparray = NULL;
            context->temproutine = NULL;
        }
    }
    else if (depth == 1) {
        if (nodetype == XML_ELEMENT_NODE) {
            const xmlChar *name = xmlTextReaderConstName(reader);
            if (!xmlStrcmp(name, BAD_CAST "constant")) {
                context->curgrouptype = grp_Constant;
                context->tempconstant = create_infoconstant();
            }
            else if (!xmlStrcmp(name, BAD_CAST "routine")) {
                context->curgrouptype = grp_Routine;
                context->temproutine = create_inforoutine();
                context->tempnumlocals = 0;
            }
            else if (!xmlStrcmp(name, BAD_CAST "global-variable")) {
                context->curgrouptype = grp_Global;
                context->tempconstant = create_infoconstant();
            }
            else if (!xmlStrcmp(name, BAD_CAST "object")) {
                context->curgrouptype = grp_Object;
                context->tempconstant = create_infoconstant();
            }
            else if (!xmlStrcmp(name, BAD_CAST "array")) {
                context->curgrouptype = grp_Array;
                context->temparray = create_infoarray();
            }
            else if (!xmlStrcmp(name, BAD_CAST "story-file-prefix")) {
                xmlNodePtr nod = xmlTextReaderExpand(reader);
                if (nod && nod->children && nod->children->type == XML_TEXT_NODE) {
                    context->storyfileprefix = xmlStrdup(nod->children->content);
                }
            }
            else {
                context->curgrouptype = grp_None;
            }
        }
        else if (nodetype == XML_ELEMENT_DECL) {
            /* End of group */
            switch (context->curgrouptype) {
            case grp_Constant:
                if (context->tempconstant) {
                    infoconstant *dat = context->tempconstant;
                    context->tempconstant = NULL;
                    xmlHashAddEntry(context->constants, dat->identifier, dat);
                }
                break;
            case grp_Global:
                if (context->tempconstant) {
                    infoconstant *dat = context->tempconstant;
                    context->tempconstant = NULL;
                    xmlHashAddEntry(context->globals, dat->identifier, dat);
                }
                break;
            case grp_Object:
                if (context->tempconstant) {
                    infoconstant *dat = context->tempconstant;
                    context->tempconstant = NULL;
                    xmlHashAddEntry(context->objects, dat->identifier, dat);
                }
                break;
            case grp_Array:
                if (context->temparray) {
                    infoarray *dat = context->temparray;
                    context->temparray = NULL;
                    xmlHashAddEntry(context->arrays, dat->identifier, dat);
                }
                break;
            case grp_Routine:
                if (context->temproutine) {
                    inforoutine *dat = context->temproutine;
                    context->temproutine = NULL;
                    /* Copy the list of locals into the inforoutine structure.
                       This loop totally assumes that locals are found
                       in order in the debug file! */
                    if (context->tempnumlocals && context->templocals) {
                        int ix;
                        dat->numlocals = context->tempnumlocals;
                        dat->locals = (infoconstant *)malloc(context->tempnumlocals * sizeof(infoconstant));
                        for (ix=0; ix<context->tempnumlocals; ix++) {
                            dat->locals[ix].identifier = context->templocals[ix].identifier;
                            dat->locals[ix].value = context->templocals[ix].value;
                            context->templocals[ix].identifier = NULL;
                            context->templocals[ix].value = 0;
                        }
                    }
                    context->tempnumlocals = 0;
                    xmlHashAddEntry(context->routines, dat->identifier, dat);
                }
                break;
            default:
                break;
            }
            context->curgrouptype = grp_None;
        }
    }
    else {
        if (nodetype == XML_ELEMENT_NODE) {
            const xmlChar *name = xmlTextReaderConstName(reader);
            /* These fields are always simple text nodes. */
            if (!xmlStrcmp(name, BAD_CAST "identifier")) {
                xmlNodePtr nod = xmlTextReaderExpand(reader);
                if (nod && nod->children && nod->children->type == XML_TEXT_NODE) {
                    xmlChar *text = nod->children->content;
                    if (context->curgrouptype == grp_Constant) {
                        if (context->tempconstant) {
                            if (depth == 2)
                                context->tempconstant->identifier = xmlStrdup(text);
                        }
                    }
                    else if (context->curgrouptype == grp_Global) {
                        if (context->tempconstant) {
                            if (depth == 2)
                                context->tempconstant->identifier = xmlStrdup(text);
                        }
                    }
                    else if (context->curgrouptype == grp_Object) {
                        if (context->tempconstant) {
                            if (depth == 2)
                                context->tempconstant->identifier = xmlStrdup(text);
                        }
                    }
                    else if (context->curgrouptype == grp_Array) {
                        if (context->temparray) {
                            if (depth == 2)
                                context->temparray->identifier = xmlStrdup(text);
                        }
                    }
                    else if (context->curgrouptype == grp_Routine) {
                        if (context->temproutine) {
                            if (depth == 2)
                                context->temproutine->identifier = xmlStrdup(text);
                        }
                    }
                }
            }
            else if (!xmlStrcmp(name, BAD_CAST "value")) {
                xmlNodePtr nod = xmlTextReaderExpand(reader);
                if (nod && nod->children && nod->children->type == XML_TEXT_NODE) {
                    xmlChar *text = xmlStrdup(nod->children->content);
                    if (context->curgrouptype == grp_Constant) {
                        if (context->tempconstant) {
                            if (depth == 2)
                                context->tempconstant->value = strtol((char *)text, NULL, 10);
                        }
                    }
                    else if (context->curgrouptype == grp_Object) {
                        if (context->tempconstant) {
                            if (depth == 2)
                                context->tempconstant->value = strtol((char *)text, NULL, 10);
                        }
                    }
                    else if (context->curgrouptype == grp_Array) {
                        if (context->temparray) {
                            if (depth == 2)
                                context->temparray->address = strtol((char *)text, NULL, 10);
                        }
                    }
                    else if (context->curgrouptype == grp_Routine) {
                        if (context->temproutine) {
                            if (depth == 2)
                                context->temproutine->address = strtol((char *)text, NULL, 10);
                        }
                    }
                }
            }
            else if (!xmlStrcmp(name, BAD_CAST "address")) {
                xmlNodePtr nod = xmlTextReaderExpand(reader);
                if (nod && nod->children && nod->children->type == XML_TEXT_NODE) {
                    xmlChar *text = xmlStrdup(nod->children->content);
                    if (context->curgrouptype == grp_Global) {
                        if (context->tempconstant) {
                            if (depth == 2)
                                context->tempconstant->value = strtol((char *)text, NULL, 10);
                        }
                    }
                }
            }
            else if (!xmlStrcmp(name, BAD_CAST "byte-count")) {
                xmlNodePtr nod = xmlTextReaderExpand(reader);
                if (nod && nod->children && nod->children->type == XML_TEXT_NODE) {
                    xmlChar *text = xmlStrdup(nod->children->content);
                    if (context->curgrouptype == grp_Routine) {
                        if (context->temproutine) {
                            if (depth == 2)
                                context->temproutine->length = strtol((char *)text, NULL, 10);
                        }
                    }
                }
            }
            else if (!xmlStrcmp(name, BAD_CAST "local-variable")) {
                xmlNodePtr nod = xmlTextReaderExpand(reader);
                if (nod) {
                    if (!context->templocals) {
                        context->templocalssize = 8;
                        context->templocals = (infoconstant *)malloc(context->templocalssize * sizeof(infoconstant));
                    }
                    if (context->tempnumlocals >= context->templocalssize) {
                        context->templocalssize = 2*context->tempnumlocals + 4;
                        context->templocals = (infoconstant *)realloc(context->templocals, context->templocalssize * sizeof(infoconstant));
                    }
                    infoconstant *templocal = &(context->templocals[context->tempnumlocals]);
                    context->tempnumlocals += 1;
                    for (nod = nod->children; nod; nod=nod->next) {
                        if (nod->type == XML_ELEMENT_NODE) {
                            if (!xmlStrcmp(nod->name, BAD_CAST "identifier") && nod->children && nod->children->type == XML_TEXT_NODE) {
                                templocal->identifier = xmlStrdup(nod->children->content);
                            }
                            if (!xmlStrcmp(nod->name, BAD_CAST "frame-offset") && nod->children && nod->children->type == XML_TEXT_NODE) {
                                templocal->value = strtol((char *)nod->children->content, NULL, 10);
                            }
                        }
                    }
                }
            }
        }
    }
}

/* This is called after the XML data is parsed. We wrap up what we've
   found and store it in the debuginfo global. Returns 1 on success.
*/
static int finalize_debuginfo(debuginfofile *context)
{
    int ix;

    context->numroutines = xmlHashSize(context->routines);
    context->routinelist = (inforoutine **)malloc(context->numroutines * sizeof(inforoutine *));

    context->tempcounter = 0;
    xmlHashScan(context->routines, add_routine_to_table, context);
    if (context->tempcounter != context->numroutines) 
        printf("### array underflow!\n"); /*###*/
    qsort(context->routinelist, context->numroutines, sizeof(inforoutine *), sort_routines_table);

    for (ix=0; ix<context->numroutines; ix++) {
        if (ix+1 < context->numroutines) {
            context->routinelist[ix]->nextaddress = context->routinelist[ix+1]->address;
        }
        else {
            context->routinelist[ix]->nextaddress = context->routinelist[ix]->address + context->routinelist[ix]->length;
        }
    }

    if (context->templocals) {
        free(context->templocals);
        context->templocals = NULL;
    }
    context->templocalssize = 0;
    context->tempnumlocals = 0;

    /* Install into global. */
    debuginfo = context;
    return 1;
}

/* Compare main memory to the story-file-prefix we found. If it doesn't
   match, display a warning. */
void debugger_check_story_file()
{
    if (!debuginfo || !debuginfo->storyfileprefix)
        return;

    /* Check that this looks like an Inform 6 game file. See the
       Glulx Inform Technical Reference. */
    if (Mem4(0x24) != 0x496E666F) { /* 'Info' */
        gidebug_output("Warning: This game file does not look like it was generated by Inform.");
    }

    /* Decode the storyfileprefix, which is in base64. */

    const unsigned char *cx;
    int pos = 0;
    int count = 0;
    uint32_t word = 0;
    int fail = FALSE;

    for (cx = debuginfo->storyfileprefix; *cx && *cx != '='; cx++) {
        unsigned int sixbit = 0;
        if (*cx >= 'A' && *cx <= 'Z')
            sixbit = (*cx) - 'A';
        else if (*cx >= 'a' && *cx <= 'z')
            sixbit = ((*cx) - 'a') + 26;
        else if (*cx >= '0' && *cx <= '9')
            sixbit = ((*cx) - '0') + 52;
        else if (*cx == '+')
            sixbit = 62;
        else if (*cx == '/')
            sixbit = 63;
        else
            sixbit = 0;

        switch (count) {
        case 0:
            word = (sixbit << 18);
            break;
        case 1:
            word |= (sixbit << 12);
            break;
        case 2:
            word |= (sixbit << 6);
            break;
        case 3:
            word |= (sixbit);
            break;
        }

        count++;
        if (count == 4) {
            unsigned char byte;
            byte = (word >> 16) & 0xFF;
            if (byte != Mem1(pos))
                fail = TRUE;
            byte = (word >> 8) & 0xFF;
            if (byte != Mem1(pos+1))
                fail = TRUE;
            byte = (word) & 0xFF;
            if (byte != Mem1(pos+2))
                fail = TRUE;
            pos += 3;
            count = 0;
        }
    }

    if (fail)
        gidebug_output("Warning: debug info <story-file-prefix> does not match this game file.");
}

/* The rest of this file is the debugger itself. */

static char *linebuf = NULL;
static int linebufsize = 0;

/* Expand the output line buffer to a given length, if necessary.
   If you want to write an N-character string, call ensure_line_buf(N).
   Then use snprintf() just to be safe. */
static void ensure_line_buf(int len)
{
    len += 1; /* for the closing null */
    if (linebuf && len <= linebufsize)
        return;

    linebufsize = linebufsize*2+16;
    if (linebufsize < len)
        linebufsize = len+16;

    if (!linebuf)
        linebuf = malloc(linebufsize * sizeof(char));
    else
        linebuf = realloc(linebuf, linebufsize * sizeof(char));
}

static int track_cpu = FALSE;
unsigned long debugger_opcount = 0; /* incremented in exec.c */
static struct timeval debugger_timer;

/* Set the track-CPU flag. (In fact we always track the VM CPU usage.
   This flag determines whether we report it to the debug console.)
*/
void debugger_track_cpu(int flag)
{
    track_cpu = flag;
}

static inforoutine *find_routine_for_address(glui32 addr)
{
    if (!debuginfo)
        return NULL;

    inforoutine **res = bsearch(&addr, debuginfo->routinelist, debuginfo->numroutines, sizeof(inforoutine *), find_routine_in_table);
    if (!res)
        return NULL;

    inforoutine *func = *res;
    if (addr < func->address || addr >= func->address + func->length)
        return NULL;

    return func;
}

static void render_value_linebuf(glui32 val)
{
    /* Always display the decimal and hex. */
    int tmplen = strlen(linebuf);
    ensure_line_buf(tmplen+40);
    snprintf(linebuf+tmplen, linebufsize-tmplen, "%d ($%X)", val, val);

    /* If the address of a function, display it. (But not addresses in
       the middle of a function.) */
    inforoutine *func = find_routine_for_address(val);
    if (func) {
        if (val == func->address) {
            tmplen = strlen(linebuf);
            ensure_line_buf(tmplen+40);
            snprintf(linebuf+tmplen, linebufsize-tmplen, ", %s()", func->identifier);
        }
    }
}

static void debugcmd_backtrace()
{
    if (stack) {
        ensure_line_buf(256);
        glui32 curpc = pc;
        glui32 curframeptr = frameptr;
        glui32 curstackptr = stackptr;
        glui32 curvalstackbase = valstackbase;
        glui32 curlocalsbase = localsbase;
        glui32 locptr;
        int locnum;
        while (1) {
            inforoutine *routine = find_routine_for_address(curpc);
            if (!routine)
                snprintf(linebuf, linebufsize, "%s()  (pc=$%.2X)", "???", curpc);
            else
                snprintf(linebuf, linebufsize, "%s()  (pc=$%.2X)", routine->identifier, curpc);
            gidebug_output(linebuf);

            strcpy(linebuf, "  ");
            /* Again, this loop assumes that all locals are 4 bytes. */
            for (locptr = curlocalsbase, locnum = 0; locptr < curvalstackbase; locptr += 4, locnum++) {
                int tmplen = strlen(linebuf);
                ensure_line_buf(tmplen+32);
                if (!routine || !routine->locals || locnum >= routine->numlocals)
                    snprintf(linebuf+tmplen, linebufsize-tmplen, "%sloc#%d=", (locnum?"; ":""), locnum);
                else
                    snprintf(linebuf+tmplen, linebufsize-tmplen, "%s%s=", (locnum?"; ":""), routine->locals[locnum].identifier);
                
                glui32 val = Stk4(locptr);
                render_value_linebuf(val);
            }
            if (!locnum) {
                strcat(linebuf, "(no locals)");
            }
            gidebug_output(linebuf);

            curstackptr = curframeptr;
            if (curstackptr < 16)
                break;
            curstackptr -= 16;
            glui32 newframeptr = Stk4(curstackptr+12);
            glui32 newpc = Stk4(curstackptr+8);
            curframeptr = newframeptr;
            curpc = newpc;
            curvalstackbase = curframeptr + Stk4(curframeptr);
            curlocalsbase = curframeptr + Stk4(curframeptr+4);
        }
    }
}

static void debugcmd_print(char *arg)
{
    ensure_line_buf(128); /* for a start */

    while (*arg == ' ')
        arg++;

    if (*arg == '\0') {
        gidebug_output("What do you want to print?");        
        return;
    }

    /* For plain numbers, and $HEX numbers, we print the value directly. */

    if (arg[0] == '$') {
        char *cx;
        glui32 val = 0;
        for (cx=arg+1; *cx; cx++) {
            if (*cx >= '0' && *cx <= '9') {
                val = 16 * val + (*cx - '0');
                continue;
            }
            if (*cx >= 'A' && *cx <= 'F') {
                val = 16 * val + (*cx - 'A' + 10);
                continue;
            }
            if (*cx >= 'a' && *cx <= 'f') {
                val = 16 * val + (*cx - 'a' + 10);
                continue;
            }
            snprintf(linebuf, linebufsize, "Invalid hex number");
            gidebug_output(linebuf);
            return;
        }
        strcpy(linebuf, "");
        render_value_linebuf(val);
        gidebug_output(linebuf);
        return;
    }
    else if (arg[0] >= '0' && arg[0] <= '9') {
        char *cx;
        glui32 val = 0;
        for (cx=arg; *cx; cx++) {
            if (*cx >= '0' && *cx <= '9') {
                val = 10 * val + (*cx - '0');
                continue;
            }
            snprintf(linebuf, linebufsize, "Invalid number");
            gidebug_output(linebuf);
            return;
        }
        strcpy(linebuf, "");
        render_value_linebuf(val);
        gidebug_output(linebuf);
        return;
    }

    /* Symbol recognition should be case-insensitive */

    /* Is it a local variable name? */
    if (debuginfo) {
        glui32 curpc = pc;
        glui32 curlocalsbase = localsbase;
        /* Should have a way to trawl up and down the stack. */
        inforoutine *routine = find_routine_for_address(curpc);
        if (routine && routine->locals) {
            int ix;
            for (ix=0; ix<routine->numlocals; ix++) {
                if (!xmlStrcmp(routine->locals[ix].identifier, BAD_CAST arg)) {
                    glui32 locptr = curlocalsbase + 4*ix;
                    glui32 val = Stk4(locptr);
                    snprintf(linebuf, linebufsize, "local %s = ", routine->locals[ix].identifier);
                    render_value_linebuf(val);
                    gidebug_output(linebuf);
                    return;
                }
            }
        }
    }

    /* Is it a constant name? */
    if (debuginfo) {
        infoconstant *cons = xmlHashLookup(debuginfo->constants, BAD_CAST arg);
        if (cons) {
            snprintf(linebuf, linebufsize, "%d ($%X): constant", cons->value, cons->value);
            gidebug_output(linebuf);
            return;
        }
    }

    /* Is it an object name? */
    if (debuginfo) {
        infoconstant *cons = xmlHashLookup(debuginfo->objects, BAD_CAST arg);
        if (cons) {
            snprintf(linebuf, linebufsize, "%d ($%X): object", cons->value, cons->value);
            gidebug_output(linebuf);
            return;
        }
    }

    /* Is it an array name? */
    if (debuginfo) {
        infoarray *arr = xmlHashLookup(debuginfo->arrays, BAD_CAST arg);
        if (arr) {
            snprintf(linebuf, linebufsize, "%d ($%X): array", arr->address, arr->address);
            gidebug_output(linebuf);
            return;
        }
    }

    /* Is it a global name? */
    if (debuginfo) {
        infoconstant *cons = xmlHashLookup(debuginfo->globals, BAD_CAST arg);
        if (cons) {
            glui32 val = Mem4(cons->value);
            snprintf(linebuf, linebufsize, "global %s = ", cons->identifier);
            render_value_linebuf(val);
            gidebug_output(linebuf);
            return;
        }
    }

    /* Is it a routine name? */
    if (debuginfo) {
        inforoutine *routine = xmlHashLookup(debuginfo->routines, BAD_CAST arg);
        if (routine) {
            snprintf(linebuf, linebufsize, "%d ($%X): routine", routine->address, routine->address);
            gidebug_output(linebuf);
            return;
        }
    }

    gidebug_output("Symbol not found");
}

/* Debug console callback: This is invoked whenever the user enters a debug
   command.
*/
void debugger_cmd_handler(char *cmd)
{
    /* Trim spaces from start */
    while (*cmd == ' ')
        cmd++;
    /* Trim spaces from end */
    int len = strlen(cmd);
    while (len > 0 && cmd[len-1] == ' ') {
        cmd[len-1] = '\0';
        len--;
    }

    if (*cmd == '\0')
        return; /* empty command */

    char *cx;
    for (cx=cmd; *cx && *cx != ' '; cx++) { }
    len = (cx - cmd);

    if (len == 2 && !strncmp(cmd, "bt", len)) {
        debugcmd_backtrace();
        return;
    }

    if (len == 5 && !strncmp(cmd, "print", len)) {
        debugcmd_print(cx);
        return;
    }

    ensure_line_buf(strlen(cmd) + 64);
    snprintf(linebuf, linebufsize, "Unknown debug command: %s", cmd);
    gidebug_output(linebuf);
}

/* Debug console callback: This is invoked when the game starts, when it
   ends, and when each input cycle begins and ends.

   We take this opportunity to report CPU usage, if the track_cpu flag
   is set.
*/
void debugger_cycle_handler(int cycle)
{
    struct timeval now;
    double diff;

    if (track_cpu) {
        switch (cycle) {
        case gidebug_cycle_Start:
            debugger_opcount = 0;
            gettimeofday(&debugger_timer, NULL);
            break;
        case gidebug_cycle_InputWait:
            gettimeofday(&now, NULL);
            diff = (now.tv_sec - debugger_timer.tv_sec) * 1000.0 + (now.tv_usec - debugger_timer.tv_usec) / 1000.0;
            ensure_line_buf(64);
            snprintf(linebuf, linebufsize, "VM: %ld cycles in %.3f ms", debugger_opcount, diff);
            gidebug_output(linebuf);
            break;
        case gidebug_cycle_InputAccept:
            debugger_opcount = 0;
            gettimeofday(&debugger_timer, NULL);
            break;
        }
    }
}

/* Report a fatal error to the debug console, along with the current
   stack trace. 
*/
void debugger_error_trace(char *msg)
{
    char *prefix = "Glulxe fatal error: ";
    ensure_line_buf(strlen(prefix) + strlen(msg));
    strcpy(linebuf, prefix);
    strcat(linebuf, msg);
    gidebug_output(linebuf);

    debugcmd_backtrace();
}


#endif /* VM_DEBUGGER */
