# Unix Makefile for Glulxe.

# To use this, you must set three variables. GLKINCLUDEDIR must be the 
# directory containing glk.h, glkstart.h, and the Make.library file.
# GLKLIBDIR must be the directory containing the library.a file.
# And GLKMAKEFILE must be the name of the Make.library file. Two
# sets of values appear below; uncomment one of them and change the
# directories appropriately.

GLKINCLUDEDIR = ../cheapglk
GLKLIBDIR = ../cheapglk
GLKMAKEFILE = Make.cheapglk

#GLKINCLUDEDIR = ../glkterm
#GLKLIBDIR = ../glkterm
#GLKMAKEFILE = Make.glkterm

#GLKINCLUDEDIR = ../xglk
#GLKLIBDIR = ../xglk
#GLKMAKEFILE = Make.xglk

#GLKINCLUDEDIR = ../remglk
#GLKLIBDIR = ../remglk
#GLKMAKEFILE = Make.remglk

# Pick a C compiler.
#CC = cc
CC = gcc

OPTIONS = -g -Wall -Wmissing-prototypes -Wstrict-prototypes -Wno-unused -DOS_UNIX

XMLLIB = -L/usr/local/Cellar/libxml2/2.9.1/lib -lxml2
XMLLIBINCLUDEDIR = -I/usr/local/Cellar/libxml2/2.9.1/include/libxml2

include $(GLKINCLUDEDIR)/$(GLKMAKEFILE)

CFLAGS = $(OPTIONS) -I$(GLKINCLUDEDIR) $(XMLLIBINCLUDEDIR)
LIBS = -L$(GLKLIBDIR) $(GLKLIB) $(LINKLIBS) -lm $(XMLLIB)

OBJS = main.o files.o vm.o exec.o funcs.o operand.o string.o glkop.o \
  heap.o serial.o search.o accel.o float.o gestalt.o osdepend.o \
  profile.o debugger.o

all: glulxe

glulxe: $(OBJS) unixstrt.o
	$(CC) $(OPTIONS) -o glulxe $(OBJS) unixstrt.o $(LIBS)

glulxdump: glulxdump.o
	$(CC) -o glulxdump glulxdump.o

$(OBJS) unixstrt.o: glulxe.h

exec.o operand.o: opcodes.h
gestalt.o: gestalt.h

clean:
	rm -f *~ *.o glulxe glulxdump profile-raw

