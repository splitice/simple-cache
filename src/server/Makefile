OBJS	= scache.o
SOURCE	= scache.cpp
HEADER	= scache.h
OUT	= scache
CC	 = g++
_CFLAGS	 = -g -c -Wall -I../core -fPIC $(CFLAGS)
LFLAGS	 += -lpthread

ifeq ($(CONFIG),"Debug")
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