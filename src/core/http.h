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
#define REQUEST_HTTPGET 0x80
#define REQUEST_HTTPPOST 0x40
#define REQUEST_HTTPPUT 0x20
#define REQUEST_HTTPDELETE 0x10

/* Request levels */
#define REQUEST_LEVELKEY 0x08
#define REQUEST_LEVELTABLE 0x04
#define REQUEST_LEVELRESERVED1 0x02
#define REQUEST_LEVELRESERVED2 0x01

/* Common types (method + level) */
#define REQUEST_GETKEY (REQUEST_HTTPGET | REQUEST_LEVELKEY)
#define REQUEST_PUTKEY (REQUEST_HTTPPUT | REQUEST_LEVELKEY)
#define REQUEST_DELETEKEY (REQUEST_HTTPDELETE | REQUEST_LEVELKEY)
#define REQUEST_GETTABLE (REQUEST_HTTPGET | REQUEST_LEVELTABLE)

/* Helpers */
#define REQUEST_IS(type, mask) ((type & (mask)) == (mask))

/* ===[ HTTP TEMPLATES ]=== */
#define NUMBER_OF_HTTPTEMPLATE 9
#define HTTPTEMPLATE_HEADERS200 0
#define HTTPTEMPLATE_FULL404 1
#define HTTPTEMPLATE_FULL200OK 2
#define HTTPTEMPLATE_NEWLINE 3
#define HTTPTEMPLATE_DBLNEWLINE 4
#define HTTPTEMPLATE_FULLINVALIDMETHOD 5
#define HTTPTEMPLATE_FULLHTTP200DELETED 6
#define HTTPTEMPLATE_HEADERS200_CONCLOSE 7
#define HTTPTEMPLATE_FULLINVALIDCONTENTLENGTH 8

static const char http_templates[NUMBER_OF_HTTPTEMPLATE][100] = {
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\n",
	"HTTP/1.1 404 File Not Found\r\nConnection: Keep-Alive\r\nContent-Length: 15\r\n\r\nKey not Found\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 4\r\n\r\nOK\r\n",
	"\r\n",
	"\r\n\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 14\r\n\r\nInvalid Method\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 9\r\n\r\nDELETED\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Close\r\n\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 24\r\n\r\nInvalid Content-Length\r\n"
};

int extern http_templates_length[NUMBER_OF_HTTPTEMPLATE];

/* Parsable headers */
#define HEADER_CONTENTLENGTH 1
#define HEADER_XTTL 2
#define HEADER_XLIMIT 3
#define HEADER_XSTART 4

/* Methods */
state_action http_read_handle(int epfd, cache_connection* connection);
state_action http_write_handle(int epfd, cache_connection* connection);
void http_templates_init();
void http_connection_handler(cache_connection* connection);
void http_cleanup(cache_connection* connection);
#endif // !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
