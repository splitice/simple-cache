OBJS	= scenario.o tests.o tests_rbuffer.o tests_system_simple.o
SOURCE	= scenario.cpp tests.cpp tests_rbuffer.cpp tests_system_simple.cpp
HEADER	= 
OUT	= tests
CC	 = g++
_CFLAGS	 = -g -c -Wall -I../src/core -fPIC $(CFLAGS)
LFLAGS	 = -lpthread

ifeq ($(CONFIG),"Debug")
	_CFLAGS += -DDEBUG_BUILD -O0
else
	_CFLAGS += -DNDEBUG -DRELEASE -O3
endif

all: $(OBJS)
	$(CC) -g $(OBJS) ../src/core/libscache.a -o $(OUT) $(LFLAGS)

scenario.o: scenario.cpp
	$(CC) $(_CFLAGS) scenario.cpp -std=c++11

test_rbuffer_standalone.o: test_rbuffer_standalone.cpp
	$(CC) $(_CFLAGS) test_rbuffer_standalone.cpp -std=c++11

tests.o: tests.cpp
	$(CC) $(_CFLAGS) tests.cpp -std=c++11

tests_rbuffer.o: tests_rbuffer.cpp
	$(CC) $(_CFLAGS) tests_rbuffer.cpp -std=c++11

tests_system_simple.o: tests_system_simple.cpp
	$(CC) $(_CFLAGS) tests_system_simple.cpp -std=c++11


clean:
	rm -f $(OBJS) $(OUT)