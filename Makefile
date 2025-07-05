CC      = cc
CFLAGS  = -std=gnu99 -Wall -Wextra -O2
LDFLAGS =
TARGETS = envmod
MAN1    = envmod.1
MANUALS = $(MAN1)
PREFIX  = /usr/local/share

all: $(TARGETS) $(MANUALS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%: %.md
	lowdown -stman -o $@ $^

compile_flags.txt:
	echo $(CFLAGS) | tr ' ' '\n' > $@

clean:
	rm -f $(TARGETS) $(MANUALS) compile_flags.txt

install: $(TARGETS) $(MANUALS)
	install -d $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -m 0755 $(TARGETS) $(PREFIX)/bin
	install $(MAN1) $(PREFIX)/share/man/man1
