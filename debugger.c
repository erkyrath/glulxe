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
    int failed;

    grouptype curgrouptype;

    infoconstant *tempconstant;
    inforoutine *temproutine;

    xmlHashTablePtr constants;
    xmlHashTablePtr routines;
} debuginfofile;

static debuginfofile *debuginfo = NULL;

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

static int xmlreadfunc(void *rock, char *buffer, int len)
{
    debuginfofile *context = rock;
    int res = glk_get_buffer_stream(context->str, buffer, len);
    if (res < 0)
        return -1;
    return res;
}

static int xmlclosefunc(void *rock)
{
    debuginfofile *context = rock;
    glk_stream_close(context->str, NULL);
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

void debugger_load_info(strid_t stream)
{
    debuginfofile *context = create_debuginfofile();
    context->str = stream;

    xmlTextReaderPtr reader;
    reader = xmlReaderForIO(xmlreadfunc, xmlclosefunc, context, 
        NULL, NULL, 
        XML_PARSE_RECOVER|XML_PARSE_NOENT|XML_PARSE_NONET|XML_PARSE_NOCDATA|XML_PARSE_COMPACT);
    if (!reader) {
        printf("Error: Unable to create XML reader.\n"); /*###*/
        free_debuginfofile(context);
        return;
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
    context->str = NULL; /* the reader closed it */

    if (context->failed) {
        printf("Error: Unable to load debug info.\n"); /*###*/
        free_debuginfofile(context);
        return;
    }

    debuginfo = context;
}

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

void debugger_error_trace(char *msg)
{
    char *prefix = "Glulxe fatal error: ";
    ensure_line_buf(strlen(prefix) + strlen(msg));
    strcpy(linebuf, prefix);
    strcat(linebuf, msg);
    gidebug_output(linebuf);

    if (stack) {
        ensure_line_buf(256); /*###*/
        glui32 curpc = pc;
        glui32 curframeptr = frameptr;
        glui32 curstackptr = stackptr;
        while (1) {
            sprintf(linebuf, "### frameptr $%.2X, stackptr $%.2X, pc $%.2X", curframeptr, curstackptr, curpc);
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

void debugger_cmd_handler(char *cmd)
{
    /*###debug stuff! */
    gidebug_output(cmd);
}

#endif /* VM_DEBUGGER */
