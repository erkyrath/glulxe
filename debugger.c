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

typedef struct xmlreadcontext_struct {
    strid_t str;
    int failed;

    grouptype curgrouptype;

    infoconstant *tempconstant;
    inforoutine *temproutine;

    xmlHashTablePtr constants;
    xmlHashTablePtr routines;
} xmlreadcontext;

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
    xmlreadcontext *context = rock;
    int res = glk_get_buffer_stream(context->str, buffer, len);
    if (res < 0)
        return -1;
    return res;
}

static int xmlclosefunc(void *rock)
{
    xmlreadcontext *context = rock;
    glk_stream_close(context->str, NULL);
    context->str = NULL;
    return 0;
}

static void xmlhandlenode(xmlTextReaderPtr reader, xmlreadcontext *context)
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
                    printf("### constant '%s' (%d)\n", dat->identifier, dat->value);
                    xmlHashAddEntry(context->constants, dat->identifier, dat);
                }
                break;
            case grp_Routine:
                if (context->temproutine) {
                    inforoutine *dat = context->temproutine;
                    context->temproutine = NULL;
                    printf("### routine '%s' (%d, len %d)\n", dat->identifier, dat->address, dat->length);
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
                        if (context->tempconstant)
                            context->tempconstant->identifier = xmlStrdup(text);
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
                        if (context->tempconstant)
                            context->tempconstant->value = strtol((char *)text, NULL, 10);
                    }
                    else if (context->curgrouptype == grp_Routine) {
                        if (context->temproutine) {
                            if (depth == 2)
                                context->temproutine->address = strtol((char *)text, NULL, 10);
                        }
                    }
                }
            }
        }
    }
}

void debugger_load_info(strid_t stream)
{
    xmlreadcontext *context = (xmlreadcontext *)malloc(sizeof(xmlreadcontext));
    context->str = stream;
    context->failed = 0;
    context->tempconstant = NULL;
    context->temproutine = NULL;
    context->curgrouptype = grp_None;
    context->constants = xmlHashCreate(16);
    context->routines = xmlHashCreate(16);

    xmlTextReaderPtr reader;
    reader = xmlReaderForIO(xmlreadfunc, xmlclosefunc, context, 
        NULL, NULL, 
        XML_PARSE_RECOVER|XML_PARSE_NOENT|XML_PARSE_NONET|XML_PARSE_NOCDATA|XML_PARSE_COMPACT);
    if (!reader) {
        printf("Error: Unable to create XML reader.\n"); /*###*/
        /*### free */
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

    if (context->failed) {
        printf("Error: Unable to load debug info.\n"); /*###*/
        /*### free */
        return;
    }
}

#endif /* VM_DEBUGGER */
