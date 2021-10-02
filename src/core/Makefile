OBJS	= connection.o db.o hash.o http.o http_parse.o http_parse_cache.o http_parse_mon.o http_respond.o read_buffer.o settings.o signal_handle.o timer.o
SOURCE	= connection.cpp db.cpp hash.cpp http.cpp http_parse.cpp http_parse_cache.cpp http_parse_mon.cpp http_respond.cpp read_buffer.cpp settings.cpp signal_handle.cpp timer.cpp
HEADER	= 
OUT	= libscache.o
CC	 = g++
_CFLAGS	 = -g -c -Wall -fPIC -c $(CFLAGS)
LFLAGS	 = -lpthread  -shared

ifeq ($(CONFIG),"Debug")
	_CFLAGS += -DDEBUG_BUILD -O0
else
	_CFLAGS += -DNDEBUG -DRELEASE -O3
endif

all: $(OBJS)
	ar rvs libscache.a $(OBJS)

connection.o: connection.cpp
	$(CC) $(_CFLAGS) connection.cpp -std=c++11

db.o: db.cpp
	$(CC) $(_CFLAGS) db.cpp -std=c++11

hash.o: hash.cpp
	$(CC) $(_CFLAGS) hash.cpp -std=c++11

http.o: http.cpp
	$(CC) $(_CFLAGS) http.cpp -std=c++11

http_parse.o: http_parse.cpp
	$(CC) $(_CFLAGS) http_parse.cpp -std=c++11

http_parse_cache.o: http_parse_cache.cpp
	$(CC) $(_CFLAGS) http_parse_cache.cpp -std=c++11

http_parse_mon.o: http_parse_mon.cpp
	$(CC) $(_CFLAGS) http_parse_mon.cpp -std=c++11

http_respond.o: http_respond.cpp
	$(CC) $(_CFLAGS) http_respond.cpp -std=c++11

read_buffer.o: read_buffer.cpp
	$(CC) $(_CFLAGS) read_buffer.cpp -std=c++11

settings.o: settings.cpp
	$(CC) $(_CFLAGS) settings.cpp -std=c++11

signal_handle.o: signal_handle.cpp
	$(CC) $(_CFLAGS) signal_handle.cpp -std=c++11

timer.o: timer.cpp
	$(CC) $(_CFLAGS) timer.cpp -std=c++11


clean:
	rm -f $(OBJS) $(OUT)