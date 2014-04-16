/* debugger.c: Glulxe debugger functions.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glulx/index.html
*/

/* Don't get excited. This is just stub code right now. I'm feeling out
   how a debugger would fit into the source. I haven't actually written
   one yet.
*/

#include "glk.h"
#include "glulxe.h"

#if VM_DEBUGGER

#include <string.h>
#include <sys/time.h>
#include "gi_debug.h" 
#include <libxml/xmlreader.h>

typedef enum grouptype_enum {
    grp_None = 0,
    grp_Constant = 1,
    grp_Routine = 2,
} grouptype;

typedef struct infoconstant_struct {
    const xmlChar *identifier;
    int32_t value;
} infoconstant;

typedef struct inforoutine_struct {
    const xmlChar *identifier;
    int32_t address;
    int32_t length;
} inforoutine;

typedef struct debuginfofile_struct {
    strid_t str;
    int32_t strread;
    int32_t strreadmax;
    int failed;

    grouptype curgrouptype;

    int tempcounter;
    infoconstant *tempconstant;
    inforoutine *temproutine;

    xmlHashTablePtr constants;
    xmlHashTablePtr routines;
    int numroutines;
    inforoutine **routinelist; /* array, ordered by address */
} debuginfofile;

static debuginfofile *debuginfo = NULL;

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
    context->temproutine = NULL;
    context->curgrouptype = grp_None;
    context->constants = xmlHashCreate(16);
    context->routines = xmlHashCreate(16);
    context->numroutines = 0;
    context->routinelist = NULL;

    return context;
}

static void free_debuginfofile(debuginfofile *context)
{
    if (!context)
        return;

    context->str = NULL;
    context->tempconstant = NULL;
    context->temproutine = NULL;
 
    /* We don't bother to free the member structures, because this
       only happens once at startup and then only on error
       conditions. */

    if (context->constants) {
        xmlHashFree(context->constants, NULL);
        context->constants = NULL;
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

static inforoutine *create_inforoutine()
{
    inforoutine *cons = (inforoutine *)malloc(sizeof(inforoutine));
    cons->identifier = NULL;
    cons->address = 0;
    cons->length = 0;
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
    glui32 addr = *(glui32 *)(keyptr);
    inforoutine **routine = (inforoutine **)obj;

    if (addr < (*routine)->address)
        return -1;
    if (addr >= (*routine)->address + (*routine)->length)
        return 1;
    return 0;
}

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

static int xmlreadstreamfunc(void *rock, char *buffer, int len)
{
    debuginfofile *context = rock;
    int res = glk_get_buffer_stream(context->str, buffer, len);
    if (res < 0)
        return -1;
    return res;
}

static int xmlreadchunkfunc(void *rock, char *buffer, int len)
{
    debuginfofile *context = rock;
    if (context->strread >= context->strreadmax)
        return -1;

    /* Something goes screwy with this read pathway, and I don't know
       what. An identifier node gets skipped. I blame xmllib. */
    if (len > context->strreadmax - context->strread)
        len = context->strreadmax - context->strread;
    int res = glk_get_buffer_stream(context->str, buffer, len);
    if (res < 0)
        return -1;
    context->strread += res;
    return res;
}

static int xmlclosefunc(void *rock)
{
    debuginfofile *context = rock;
    context->str = NULL;
    return 0;
}

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
            case grp_Routine:
                if (context->temproutine) {
                    inforoutine *dat = context->temproutine;
                    context->temproutine = NULL;
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
                    else if (context->curgrouptype == grp_Routine) {
                        if (context->temproutine) {
                            if (depth == 2)
                                context->temproutine->address = strtol((char *)text, NULL, 10);
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
        }
    }
}

static int finalize_debuginfo(debuginfofile *context)
{
    context->numroutines = xmlHashSize(context->routines);
    context->routinelist = (inforoutine **)malloc(context->numroutines * sizeof(inforoutine *));
    context->tempcounter = 0;
    xmlHashScan(context->routines, add_routine_to_table, context);
    if (context->tempcounter != context->numroutines) 
        printf("### array underflow!\n"); /*###*/
    qsort(context->routinelist, context->numroutines, sizeof(inforoutine *), sort_routines_table);

    /* Install into global. */
    debuginfo = context;
    return 1;
}

/* The rest of this file is the debugger itself. */

static char *linebuf = NULL;
static int linebufsize = 0;

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
    return *res;
}

static void debugcmd_backtrace()
{
    if (stack) {
        ensure_line_buf(256);
        glui32 curpc = pc;
        glui32 curframeptr = frameptr;
        glui32 curstackptr = stackptr;
        while (1) {
            inforoutine *routine = find_routine_for_address(curpc);
            if (!routine)
                snprintf(linebuf, linebufsize, "?()  (pc=$%.2X)", curpc);
            else
                snprintf(linebuf, linebufsize, "%s()  (pc=$%.2X)", routine->identifier, curpc);
            gidebug_output(linebuf);

            curstackptr = curframeptr;
            if (curstackptr < 16)
                break;
            curstackptr -= 16;
            glui32 newframeptr = Stk4(curstackptr+12);
            glui32 newpc = Stk4(curstackptr+8);
            curframeptr = newframeptr;
            curpc = newpc;
        }
    }
}

static void debugcmd_print(char *arg)
{
    while (*arg == ' ')
        arg++;

    if (*arg == '\0') {
        gidebug_output("What do you want to print?");        
        return;
    }

    /* Symbol recognition should be case-insensitive */

    if (debuginfo) {
        infoconstant *cons = xmlHashLookup(debuginfo->constants, BAD_CAST arg);
        if (cons) {
            ensure_line_buf(128);
            snprintf(linebuf, linebufsize, "%d ($%X): constant", cons->value, cons->value);
            gidebug_output(linebuf);
            return;
        }
    }

    if (debuginfo) {
        inforoutine *routine = xmlHashLookup(debuginfo->routines, BAD_CAST arg);
        if (routine) {
            ensure_line_buf(128);
            snprintf(linebuf, linebufsize, "%d ($%X): routine", routine->address, routine->address);
            gidebug_output(linebuf);
            return;
        }
    }

    gidebug_output("Symbol not found");
}

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
