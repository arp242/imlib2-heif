CC        ?= cc
CFLAGS     = -std=c99 -O2 $(shell pkg-config libheif --cflags) -fPIC -Wall -Wextra -Wpedantic -g
LDFLAGS    = $(shell imlib2-config --libs) $(shell pkg-config libheif --libs)
PLUGINDIR ?= $(shell pkg-config --variable=libdir imlib2)/imlib2/loaders/

all:
	${CC} -shared -o heif.so ${CFLAGS} ${LDFLAGS} *.c

clean:
	rm -f heif.so

install:
	mkdir -p ${DESTDIR}${PLUGINDIR}
	install -m 755 heif.so ${DESTDIR}${PLUGINDIR}

uninstall:
	rm ${PLUGINDIR}/heif.so
