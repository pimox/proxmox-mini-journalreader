PROGRAM=journalreader
SOURCES=journalreader.c

LIBS := libsystemd
CFLAGS += $(shell pkg-config --cflags ${LIBS})
LFLAGS += $(shell pkg-config --libs ${LIBS})

all: ${PROGRAM}

${PROGRAM}: ${SOURCES}
	gcc -Werror -Wall -Wl,-z,relro -Wtype-limits $< -o $@ ${CLFAGS} ${LFLAGS} -g

.PHONY: install
install: ${PROGRAM}
	mkdir -p ${DESTDIR}/usr/bin
	install -m 0755 ${PROGRAM} ${DESTDIR}/usr/bin

.PHONY: distclean
distclean: clean

.PHONY: clean
clean:
	rm -rf ${PROGRAM}
