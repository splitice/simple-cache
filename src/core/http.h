#if !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
#define HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8

#include "connection.h"

/* ===[ Requests ]=== */
/*
8 bit integer consisting of the bits

(X) GET
(X) POST
(X) PUT
(X) DELETE
(X) KEY
(X) TABLE
(X) RESERVED1
(X) RESERVED2
*/

/* Request methods */
#define LONGEST_REQMETHOD 6
#define REQUEST_HTTPADMIN 0x0200
#define REQUEST_HTTPGET 0x0100
#define REQUEST_HTTPPURGE 0x0080
#define REQUEST_HTTPPUT 0x0040
#define REQUEST_HTTPDELETE 0x0020
#define REQUEST_HTTPHEAD 0x0010

/* Request levels (Cache) */
#define REQUEST_CACHE_LEVELKEY 0x04
#define REQUEST_CACHE_LEVELTABLE 0x02
#define REQUEST_CACHE_LEVELRESERVED1 0x01

/* Request levels (Monitoring) */
#define REQUEST_MON_LEVELCONN 0x04
#define REQUEST_MON_LEVELSIMPLE 0x02
#define REQUEST_MON_LEVELRESERVED1 0x01

/* Common types (method + level) */
#define REQUEST_HEADKEY (REQUEST_HTTPHEAD | REQUEST_CACHE_LEVELKEY)
#define REQUEST_GETKEY (REQUEST_HTTPGET | REQUEST_CACHE_LEVELKEY)
#define REQUEST_PUTKEY (REQUEST_HTTPPUT | REQUEST_CACHE_LEVELKEY)
#define REQUEST_DELETEKEY (REQUEST_HTTPDELETE | REQUEST_CACHE_LEVELKEY)
#define REQUEST_GETTABLE (REQUEST_HTTPGET | REQUEST_CACHE_LEVELTABLE)

/* Helpers */
#define REQUEST_IS(type, request_type) (((((request_type) & 0x0FF0) == 0) || ((type) & (0x0FF0 & (request_type))) == ((request_type) & 0x0FF0)) && ((((request_type) & 0x000F) == 0) || ((type) & (0x000F & (request_type))) == ((request_type) & 0x000F)))

/* ===[ HTTP TEMPLATES ]=== */
#define HTTPTEMPLATE_HEADERS200 0
#define HTTPTEMPLATE_FULL404 1
#define HTTPTEMPLATE_FULL200OK 2
#define HTTPTEMPLATE_NEWLINE 3
#define HTTPTEMPLATE_DBLNEWLINE 4
#define HTTPTEMPLATE_FULLINVALIDMETHOD 5
#define HTTPTEMPLATE_FULLHTTP200DELETED 6
#define HTTPTEMPLATE_HEADERS200_CONCLOSE 7
#define HTTPTEMPLATE_FULLINVALIDCONTENTLENGTH 8
#define HTTPTEMPLATE_BULK_OK 9
#define HTTPTEMPLATE_200CONTENT_LENGTH 10
#define HTTPTEMPLATE_FULLLONGMETHOD 11
#define HTTPTEMPLATE_FULLUNKNOWNMETHOD 12
#define HTTPTEMPLATE_FULLREQUESTTOOLARGE 13
#define HTTPTEMPLATE_FULLUNKNOWNREQUEST 14
#define HTTPTEMPLATE_HEAD_ONLY 15
#define HTTPTEMPLATE_MON_STREAM 16
#define HTTPTEMPLATE_FULL404_PATHNOTFOUND 17

#define HTTP_TEMPLATE_STRLEN 128
static const char http_templates[][HTTP_TEMPLATE_STRLEN] = {
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\n",
	"HTTP/1.1 404 Not Found\r\nConnection: Keep-Alive\r\nContent-Length: 15\r\n\r\nKey not Found\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 4\r\n\r\nOK\r\n",
	"\r\n",
	"\r\n\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 16\r\n\r\nInvalid Method\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 9\r\n\r\nDELETED\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Close\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 24\r\n\r\nInvalid Content-Length\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 9\r\n\r\nBULK OK\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: %d\r\n\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 17\r\n\r\nMethod too Long\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 16\r\n\r\nUnknown Method\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 19\r\n\r\nRequest too large\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 17\r\n\r\nUnknown Request\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\n\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Type: text/x-streaming\r\nX-Hostname: %s\r\n\r\n",
	"HTTP/1.1 404 Not Found\r\nConnection: Keep-Alive\r\nContent-Length: 16\r\n\r\nPath Not Found\r\n",
};

#define MON_STREAMING_HEADERS "#state:starting\n#delay:%f\n\n#server:%s\n"

#define NUMBER_OF_HTTPTEMPLATE sizeof(http_templates)/HTTP_TEMPLATE_STRLEN

int extern http_templates_length[NUMBER_OF_HTTPTEMPLATE];

/* Parsable headers */
#define HEADER_CONTENTLENGTH 1
#define HEADER_XTTL 2
#define HEADER_XLIMIT 3
#define HEADER_XSTART 4
#define HEADER_XDELETE 5

/* Methods */
state_action http_read_handle(scache_connection* connection);
state_action http_write_handle(scache_connection* connection);
void http_templates_init();
void http_connection_handler(scache_connection* connection);
void http_cleanup(scache_connection* connection);

bool http_register_read(scache_connection* connection);


void monitoring_init();

#endif // !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
