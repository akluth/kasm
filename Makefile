CC ?= cc
PREFIX ?= /usr/local
VERSION = 0.1.0

WARN = -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude -DKASM_INSTALL_LIB=\"$(PREFIX)/share/kasm/lib\"
CFLAGS ?= -std=c11 $(WARN) -O2
LDFLAGS ?=

SRC = src/main.c src/lexer.c src/parser.c src/symbols.c src/encoder.c src/elf64.c src/diagnostics.c src/explain.c src/hints.c
OBJ = $(SRC:.c=.o)
DIST_NAME = kasm-$(VERSION)

.PHONY: all test check clean debug release release-static size bench install uninstall dist asan ubsan

all: kasm

kasm: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 $(WARN) -O0 -g' kasm

release:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 $(WARN) -Os' kasm
	strip kasm 2>/dev/null || true

release-static:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 $(WARN) -Os' LDFLAGS='-static' kasm || \
		{ echo "static build unavailable in this environment"; exit 0; }
	strip kasm 2>/dev/null || true

asan:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 $(WARN) -O1 -g -fsanitize=address -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address' kasm

ubsan:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 $(WARN) -O1 -g -fsanitize=undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=undefined' kasm

test: kasm
	sh tests/run_tests.sh

check: test

size: kasm
	@wc -c kasm
	@command -v size >/dev/null 2>&1 && size kasm || true

bench: kasm
	sh bench/run_benchmarks.sh

install: kasm
	install -d '$(DESTDIR)$(PREFIX)/bin'
	install -m 755 kasm '$(DESTDIR)$(PREFIX)/bin/kasm'
	install -d '$(DESTDIR)$(PREFIX)/share/kasm/lib'
	cp -R lib/kasm/. '$(DESTDIR)$(PREFIX)/share/kasm/lib/'
	install -d '$(DESTDIR)$(PREFIX)/share/man/man1'
	install -m 644 docs/kasm.1 '$(DESTDIR)$(PREFIX)/share/man/man1/kasm.1'

uninstall:
	rm -f '$(DESTDIR)$(PREFIX)/bin/kasm'
	rm -rf '$(DESTDIR)$(PREFIX)/share/kasm'
	rm -f '$(DESTDIR)$(PREFIX)/share/man/man1/kasm.1'

dist:
	rm -rf dist
	mkdir -p dist/$(DIST_NAME)
	cp -R src include lib examples tests bench docs .github README.md CHANGELOG.md LICENSE Makefile .gitignore dist/$(DIST_NAME)/
	rm -rf dist/$(DIST_NAME)/tests/tmp dist/$(DIST_NAME)/tests/tmp_* dist/$(DIST_NAME)/bench/tmp
	rm -rf dist/$(DIST_NAME)/examples/*/build
	find dist/$(DIST_NAME) -type f \( -name '*.o' -o -name 'kasm' -o -name 'kasm.exe' \) -delete
	tar -C dist -czf dist/$(DIST_NAME).tar.gz $(DIST_NAME)

clean:
	rm -f $(OBJ) kasm hello exit sugar explain out.bin *.o object_start main_puts tests_obj tests_*
	rm -rf tests/tmp tests/tmp_* bench/tmp dist
