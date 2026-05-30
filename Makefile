PROJECT = smlp

SRCDIR = src
BINDIR = bin

CC = gcc

CFLAGS = -O2 -Wall -Wextra

LIBS = -lws2_32 -lssl -lcrypto

all: server client

# --------------------------------------------------
# SERVER
# --------------------------------------------------

server: server.o protocol.o
	$(CC) $(CFLAGS) \
		$(BINDIR)/server.o \
		$(BINDIR)/protocol.o \
		-o $(BINDIR)/smlp_server.exe \
		$(LIBS)

server.o: $(SRCDIR)/server/server.c
	$(CC) $(CFLAGS) \
		-c $(SRCDIR)/server/server.c \
		-o $(BINDIR)/server.o

# --------------------------------------------------
# CLIENT
# --------------------------------------------------

client: client.o protocol.o
	$(CC) $(CFLAGS) \
		$(BINDIR)/client.o \
		$(BINDIR)/protocol.o \
		-o $(BINDIR)/smlp_client.exe \
		$(LIBS)

client.o: $(SRCDIR)/client/client.c
	$(CC) $(CFLAGS) \
		-c $(SRCDIR)/client/client.c \
		-o $(BINDIR)/client.o

# --------------------------------------------------
# COMMON
# --------------------------------------------------

protocol.o: $(SRCDIR)/common/protocol.c
	$(CC) $(CFLAGS) \
		-c $(SRCDIR)/common/protocol.c \
		-o $(BINDIR)/protocol.o

# --------------------------------------------------
# CLEAN
# --------------------------------------------------

clean:
	del /Q $(BINDIR)\*.o 2>nul
	del /Q $(BINDIR)\*.exe 2>nul