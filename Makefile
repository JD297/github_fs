.POSIX:

CC            = cc
CFLAGS        = -Wall -Wextra -Wpedantic -g -I/usr/local/include
LDFLAGS       = -L/usr/local/lib -lfuse -lcurl -ljson-c

TARGET        = github_fs
PREFIX        = /usr/local
BINDIR        = $(PREFIX)/bin
MANDIR        = $(PREFIX)/share/man
SRCDIR        = src
BUILDDIR      = build

OBJFILES      = $(BUILDDIR)/github_fs.o

HEADERS       =

$(BUILDDIR)/$(TARGET): $(OBJFILES)
	$(CC) -o $@ $(OBJFILES) $(LDFLAGS)

$(BUILDDIR)/github_fs.o: $(HEADERS) $(SRCDIR)/github_fs.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/github_fs.c

clean:
	rm -f $(BUILDDIR)/*

install: $(BUILDDIR)/$(TARGET)
	cp $(BUILDDIR)/$(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
