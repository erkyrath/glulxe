# Unix Makefile for Glulxe.

# To use this, you can set the variable GLK to the Glk library to use,
# such as "cheapglk", "glkterm", "xglk", "remglk", or "gtkglk". The
# variable is used to set GLKINCLUDEDIR (the directory containing glk.h,
# glkstart.h, and the Make.library file), GLKLIBDIR (the directory
# containing the library.a file), and GLKMAKEFILE (the name of the
# Make.library file).

GLK = cheapglk
GLKINCLUDEDIR = ../$(GLK)
GLKLIBDIR = ../$(GLK)
GLKMAKEFILE = Make.$(GLK)
ifeq (gtkglk, $(GLK))
GLKINCLUDEDIR = ../$(GLK)/src
endif

# Pick a C compiler.
CC = cc
#CC = gcc

OPTIONS = -g -Wall -Wmissing-prototypes -Wstrict-prototypes -Wno-unused -DOS_UNIX

# Locate the libxml2 library. You only need these lines if you are using
# the VM_DEBUGGER option. If so, uncomment these and set appropriately.
#XMLLIB = -L/usr/local/lib -lxml2
#XMLLIBINCLUDEDIR = -I/usr/local/include/libxml2

include $(GLKINCLUDEDIR)/$(GLKMAKEFILE)

CFLAGS = $(OPTIONS) -I$(GLKINCLUDEDIR) $(XMLLIBINCLUDEDIR)
LIBS = -L$(GLKLIBDIR) $(GLKLIB) $(LINKLIBS) -lm $(XMLLIB)

OBJS = main.o files.o vm.o exec.o funcs.o operand.o string.o glkop.o \
  heap.o serial.o search.o accel.o float.o gestalt.o osdepend.o \
  profile.o debugger.o

all: glulxe

glulxe: $(OBJS) unixstrt.o unixautosave.o
	$(CC) $(OPTIONS) -o glulxe $(OBJS) unixstrt.o unixautosave.o $(LIBS)

glulxdump: glulxdump.o
	$(CC) -o glulxdump glulxdump.o

$(OBJS) unixstrt.o unixautosave.o: glulxe.h unixstrt.h

exec.o operand.o: opcodes.h
gestalt.o: gestalt.h

clean:
	rm -f *~ *.o glulxe glulxdump profile-raw

