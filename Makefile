CC      = cc
CFLAGS  = -std=gnu99 -Wall -Wextra -O2
LDFLAGS =
TARGETS = envmod
TESTTARGETS = testdata/printhello
MAN1    = envmod.1
MANUALS = $(MAN1)
PREFIX  = /usr/local/share

all: $(TARGETS) $(MANUALS)

envmod: envmod.c signames.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

testdata/%: testdata/%.c
	$(CC) $(CFLAGS) -o $@ $^ -static

%: %.md
	lowdown -stman -o $@ $^

compile_flags.txt:
	echo $(CFLAGS) | tr ' ' '\n' > $@

.PHONY: test clean install

test: $(TARGETS) $(TESTTARGETS) testdata/tests.py
	pytest -vv testdata/tests.py

clean:
	rm -f $(TARGETS) $(TESTTARGETS) $(MANUALS) compile_flags.txt

install: $(TARGETS) $(MANUALS)
	install -d $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -m 0755 $(TARGETS) $(PREFIX)/bin
	install $(MAN1) $(PREFIX)/share/man/man1
