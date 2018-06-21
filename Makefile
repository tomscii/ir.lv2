# Please edit PREFIX and/or INSTDIR to your liking,
# then issue 'make install' as root (or 'sudo make install').

# Issue 'make convert4chan' to compile the 4-channel converter utility
# (for local use, not installed by make install)

BUNDLE = ir.lv2
PREFIX ?= /usr

user = $(shell whoami)
ifeq ($(user),root)
INSTDIR ?= $(DESTDIR)$(PREFIX)/lib/lv2
else 
INSTDIR ?= ~/.lv2
endif

INST_FILES = ir.so ir_gui.so ir.ttl manifest.ttl

# change "-O2 -ffast-math" to "-g -O0" below if you want to debug the plugin
CPPFLAGS += -Wall -I. -I/usr/include `pkg-config --cflags gtk+-2.0` `pkg-config --cflags gthread-2.0` -D__STDC_FORMAT_MACROS -O2 -ffast-math
LIBS += -lc -lm -lzita-convolver -lsamplerate -lsndfile `pkg-config --libs gthread-2.0` `pkg-config --libs gtk+-2.0`

ifeq ($(shell pkg-config --atleast-version='2.16' gtk+-2.0; echo $$?), 1)
   $(error "At least GTK+ version 2.16 is needed to build IR.")
endif

ifeq ($(shell pkg-config --atleast-version='2.20' gtk+-2.0; echo $$?), 0)
   CPPFLAGS += -D_HAVE_GTK_ATLEAST_2_20
endif

C4CFLAGS = -Wall -I. -I/usr/include `pkg-config --cflags gthread-2.0` -O2 -ffast-math
C4LIBS = -lsndfile `pkg-config --libs gthread-2.0`

all: ir.so ir_gui.so

ir.o: ir.cc ir.h ir_utils.h
	g++ ir.cc $(CPPFLAGS) -c -fPIC -o ir.o

ir_gui.o: ir_gui.cc ir.h ir_utils.h ir_wavedisplay.h
	g++ ir_gui.cc $(CPPFLAGS) -c -fPIC -o ir_gui.o

ir_utils.o: ir_utils.cc ir_utils.h ir.h
	g++ ir_utils.cc $(CPPFLAGS) -c -fPIC -o ir_utils.o

ir_meter.o: ir_meter.cc ir_meter.h ir.h ir_utils.h
	g++ ir_meter.cc $(CPPFLAGS) -c -fPIC -o ir_meter.o

ir_modeind.o: ir_modeind.cc ir_modeind.h ir.h ir_utils.h
	g++ ir_modeind.cc $(CPPFLAGS) -c -fPIC -o ir_modeind.o

ir_wavedisplay.o: ir_wavedisplay.cc ir_wavedisplay.h ir.h ir_utils.h
	g++ ir_wavedisplay.cc $(CPPFLAGS) -c -fPIC -o ir_wavedisplay.o

ir.so: ir.o ir_utils.o
	g++ $(LDFLAGS) ir.o ir_utils.o $(LIBS) -shared -o ir.so

ir_gui.so: ir_gui.o ir_utils.o ir_meter.o ir_modeind.o ir_wavedisplay.o
	g++ $(LDFLAGS) ir_gui.o ir_utils.o ir_meter.o ir_modeind.o ir_wavedisplay.o $(LIBS) -shared -z nodelete -o ir_gui.so

convert4chan: convert4chan.c
	gcc $(C4CFLAGS) $(CPPFLAGS) $(LDFLAGS) convert4chan.c $(C4LIBS) -o convert4chan

install: all
	mkdir -p $(INSTDIR)/$(BUNDLE)
	cp $(INST_FILES) $(INSTDIR)/$(BUNDLE)

clean:
	rm -f *~ *.o ir.so ir_gui.so convert4chan

uninstall :
	rm -rf $(INSTDIR)/$(BUNDLE)
