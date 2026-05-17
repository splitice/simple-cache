OBJS	= scache.o
SOURCE	= scache.cpp
HEADER	= scache.h
OUT	= scache
CC	 = g++
_CFLAGS	 = -g -c -Wall -I../core -fPIC $(CFLAGS)
LFLAGS	 += -lpthread

GIT_DESCRIBE := $(shell git describe --tags --always --dirty 2>/dev/null || echo 0.0.0)
GIT_REV := $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)

_CFLAGS += -DSCACHE_VERSION=\"$(GIT_DESCRIBE)\" -DSCACHE_REVISION=\"$(GIT_REV)\"

ifeq ($(CONFIG),Debug)
	_CFLAGS += -DDEBUG_BUILD -O0
else
	_CFLAGS += -DNDEBUG -DRELEASE -O3
endif

all: $(OBJS)
	$(CC) -g  $(OBJS) ../core/libscache.a -o $(OUT) $(LFLAGS)

scache.o: ../core/libscache.a scache.cpp
	$(CC) $(_CFLAGS) scache.cpp -std=c++11


clean:
	rm -f $(OBJS) $(OUT)
