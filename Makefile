# Makefile for adlmidi plugin
ifeq ($(OS),Windows_NT)
    SUFFIX = dll
else
    SUFFIX = so
endif

CC=gcc
STD=gnu99
CFLAGS=-fPIC -I /usr/local/include -Wall
ifeq ($(DEBUG),1)
CFLAGS +=-g -O0
endif

PREFIX=/usr/local/lib/deadbeef
PLUGNAME=adlmidi
LIBS=-lADLMIDI 
# /usr/local/lib/libADLMIDI.a
C_FILES=
C_FILES_OUT = $(C_FILES:.c=.o)

all:
	$(CC) -std=$(STD) -c $(CFLAGS) -c $(PLUGNAME).c $(C_FILES)
	$(CC) -std=$(STD) -shared $(CFLAGS) -o $(PLUGNAME).$(SUFFIX) $(PLUGNAME).o $(C_FILES_OUT) $(LIBS)

install:
	cp $(PLUGNAME).$(SUFFIX) $(PREFIX)

help:
	@echo "Makefile for $(PLUGNAME) plugin"
	@echo "For debugging symbols compile with DEBUG=1"

clean:
	rm -fv $(PLUGNAME).o $(PLUGNAME).$(SUFFIX)
