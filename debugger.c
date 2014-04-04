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

    /* These are fields, not groups */
    grp_Identifier = 21,
    grp_Value = 22,
} grouptype;

typedef struct xmlreadcontext_struct {
    strid_t str;
    int failed;

    grouptype curgrouptype;
    grouptype curfieldtype;
    const xmlChar *tempidentifier;
    const xmlChar *tempvalue;

    xmlHashTablePtr identifiers;
} xmlreadcontext;

static void set_field_identifier(xmlreadcontext *context, const xmlChar *text)
{
    if (!text) {
        if (context->tempidentifier) {
            xmlFree((void *)context->tempidentifier);
            context->tempidentifier = NULL;
        }
    }
    else {
        if (context->tempidentifier) {
            xmlFree((void *)context->tempidentifier);
        }
        context->tempidentifier = xmlStrdup(text);
    }
}

static void set_field_value(xmlreadcontext *context, const xmlChar *text)
{
    if (!text) {
        if (context->tempvalue) {
            xmlFree((void *)context->tempvalue);
            context->tempvalue = NULL;
        }
    }
    else {
        if (context->tempvalue) {
            xmlFree((void *)context->tempvalue);
        }
        context->tempvalue = xmlStrdup(text);
    }
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
    /*###
    const xmlChar *name, *value;
    name = xmlTextReaderConstName(reader);
    if (name == NULL)
        name = BAD_CAST "--";
    value = xmlTextReaderConstValue(reader);
    printf("%d %d %s %d %d",
        xmlTextReaderDepth(reader),
        xmlTextReaderNodeType(reader),
        name,
        xmlTextReaderIsEmptyElement(reader),
        xmlTextReaderHasValue(reader));
    if (value == NULL)
        printf("\n");
    else {
        if (xmlStrlen(value) > 40)
            printf(" %.40s...\n", value);
        else
            printf(" %s\n", value);
    }
    ###*/

    int depth = xmlTextReaderDepth(reader);
    int nodetype = xmlTextReaderNodeType(reader);

    if (depth == 0) {
        if (nodetype == XML_ELEMENT_NODE) {
            const xmlChar *name = xmlTextReaderConstName(reader);
            if (xmlStrcmp(name, (xmlChar *)"inform-story-file")) {
                printf("Error: This is not an Inform debug info file.\n"); /*###*/
                context->failed = 1;
            }
        }
        else if (nodetype == XML_ELEMENT_DECL) {
            /* End of document */
            context->curgrouptype = grp_None;
        }
    }
    else if (depth == 1) {
        if (nodetype == XML_ELEMENT_NODE) {
            const xmlChar *name = xmlTextReaderConstName(reader);
            if (xmlStrcmp(name, (xmlChar *)"constant"))
                context->curgrouptype = grp_Constant;
            else
                context->curgrouptype = grp_None;
        }
        else if (nodetype == XML_ELEMENT_DECL) {
            /* End of group */
            switch (context->curgrouptype) {
            case grp_Constant:
                if (context->tempidentifier && context->tempvalue) {
                    printf("### constant %s, %s\n", context->tempidentifier, context->tempvalue);
                }
                break;
            default:
                break;
            }
            context->curgrouptype = grp_None;
            set_field_identifier(context, NULL);
            set_field_value(context, NULL);
        }
    }
    else {
        if (nodetype == XML_ELEMENT_NODE) {
            const xmlChar *name = xmlTextReaderConstName(reader);
            /* These fields are always simple text nodes. */
            if (xmlStrcmp(name, (xmlChar *)"identifier"))
                context->curfieldtype = grp_Identifier;
            else if (xmlStrcmp(name, (xmlChar *)"value"))
                context->curfieldtype = grp_Value;
        }
        else if (nodetype == XML_TEXT_NODE) {
            const xmlChar *text = xmlTextReaderConstValue(reader); 
            if (context->curfieldtype == grp_Identifier) {
                set_field_identifier(context, text);
                context->curfieldtype = grp_None;
            }
            else if (context->curfieldtype == grp_Value) {
                set_field_value(context, text);
                context->curfieldtype = grp_None;
            }
        }
        else if (nodetype == XML_ELEMENT_DECL) {
        }
    }
}

void debugger_load_info(strid_t stream)
{
    xmlreadcontext *context = (xmlreadcontext *)malloc(sizeof(xmlreadcontext));
    context->str = stream;
    context->failed = 0;
    context->tempvalue = NULL;
    context->tempidentifier = NULL;
    context->curgrouptype = grp_None;
    context->curfieldtype = grp_None;
    context->identifiers = xmlHashCreate(16);

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
