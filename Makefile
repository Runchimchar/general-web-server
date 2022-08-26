# Jenna Whilden (jpwolf101@gmail.com) 11-22-2021
# Simple HTML web server makefile

CC = gcc
CFLAGS = -g -Wall
OS = $(shell uname -s)
PROC = $(shell uname -p)
EXEC_SUFFIX=$(OS)-$(PROC)

ifeq ("$(OS)", "SunOS")
	OSLIB=-L/opt/csw/lib -R/opt/csw/lib -lsocket -lnsl
	OSINC=-I/opt/csw/include
	OSDEF=-DSOLARIS
else
ifeq ("$(OS)", "Darwin")
	OSLIB=
	OSINC=
	OSDEF=-DDARWIN
else
	OSLIB=
	OSINC=
	OSDEF=-DLINUX
endif
endif

all:  web-server-$(EXEC_SUFFIX)

web-server-$(EXEC_SUFFIX): web-server.o
	$(CC) $(CFLAGS) $(OSINC) $(OSLIB) $(OSDEF) -o $@ web-server.o

web-server.o: web-server.c web-server.h
	$(CC) $(CFLAGS) $(OSINC) $(OSLIB) $(OSDEF) -c web-server.c

clean:
	-rm -rf web-server-* *.o
