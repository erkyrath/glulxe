#include "glk.h"
#include "glulxe.h"

#if VM_DEBUGGER

#include <libxml/xmlreader.h>

typedef struct xmlreadcontext_struct {
    strid_t str;
} xmlreadcontext;

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
    return 0;
}

static void xmlhandlenode(xmlTextReaderPtr reader)
{
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

}

void debugger_load_info(strid_t stream)
{
    xmlreadcontext context;
    context.str = stream;

    xmlTextReaderPtr reader;
    reader = xmlReaderForIO(xmlreadfunc, xmlclosefunc, &context, 
        NULL, NULL, 
        XML_PARSE_RECOVER|XML_PARSE_NOENT|XML_PARSE_NONET|XML_PARSE_NOCDATA|XML_PARSE_COMPACT);
    if (!reader) {
        return; /* error */
    }

    while (1) {
        int res = xmlTextReaderRead(reader);
        if (res < 0) {
            break; /* error */
        }
        if (res == 0) {
            break; /* EOF */
        }
        xmlhandlenode(reader);
    }

    xmlFreeTextReader(reader);
}

#endif /* VM_DEBUGGER */
