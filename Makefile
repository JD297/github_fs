.POSIX:

CC            = c++
CFLAGS        = -Wall -Wextra -Wpedantic -g -I/usr/local/include -Iinclude
LDFLAGS       = -L/usr/local/lib -lfuse -lcurl

TARGET        = github_fs
PREFIX        = /usr/local
BINDIR        = $(PREFIX)/bin
MANDIR        = $(PREFIX)/share/man
SRCDIR        = src
BUILDDIR      = build

OBJFILES      = $(BUILDDIR)/github_fs.o

HEADERS       = include/nlohmann/json.hpp

$(BUILDDIR)/$(TARGET): $(OBJFILES)
	$(CC) -o $@ $(OBJFILES) $(LDFLAGS)

$(BUILDDIR)/github_fs.o: $(HEADERS) $(SRCDIR)/github_fs.cpp
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/github_fs.cpp

$(BUILDDIR)/json-nlohmann.o: include/nlohmann/json.hpp
	$(CC) -c include/nlohmann/json.hpp -o $@

include/nlohmann/json.hpp:
	mkdir -p include/nlohmann
	ftp -o include/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp

clean:
	rm -rf include/nlohmann
	rm -f $(BUILDDIR)/*

install: $(BUILDDIR)/$(TARGET)
	cp $(BUILDDIR)/$(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
