#if !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
#define HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8

#include "connection.h"

/* ===[ READ ]=== */
/*
Parse Request line
Format: $METHOD $PATH HTTP/$VERSION
Used By: ALL

INVALID: register for write, proceed to STATE_RESPONSEWRITEONLY
OTHER: proceed to STATE_REQUESTSTARTURL
*/
#define STATE_REQUESTSTARTMETHOD 0x1

/*
Extract the URL
Used By: GET, PUT, DELETE

GET: proceed to STATE_REQUESTENDSEARCH
PUT: proceed to STATE_REQUESTHEADERS
DELETE: proceed to STATE_REQUESTENDSEARCH
*/
#define STATE_REQUESTSTARTURL1 0x2

/*
Extract needed information from headers
Format: $HEADER: $VALUE
Used By: GET,PUT

PUT: proceed to STATE_REQUESTBODY
*/
#define STATE_REQUESTHEADERS 0x3

/*
Extract decimal value from the Content-Length header
Format: int
Used By: PUT
*/
#define STATE_REQUESTHEADERS_CONTENTLENGTH 0x20

/*
Handle the request body content (uploaded data)
Format: Handle $CONTENT_LENGTH worth of data
Used By: PUT

PUT: register for write, proceed to STATE_RESPONSEWRITEONLY
*/
#define STATE_REQUESTBODY 0x6


#define STATE_HTTPVERSION 0x7

#define STATE_HTTPEOLWRITE 0x8

#define STATE_REQUESTHEADERS_ZERO 0xa

#define STATE_REQUESTSTARTURL2 0xb

/* ===[ WRITE ]=== */
/*
Send the response line, and any headers in the template
Used By: GET

GET: proceed to STATE_RESPONSEHEADER_CONTENTLENGTH
*/
#define STATE_RESPONSESTART 0x10

/*
Send dynamic Content-Length header
Used By: GET

GET: proceed to STATE_RESPONSEEND
*/
#define STATE_RESPONSEHEADER_CONTENTLENGTH 0x11

#define STATE_REQUESTHEADERS_XTTL 0x12

/*
Send \r\n to signal the end of the response headers
Used By: GET

GET: proceed to STATE_RESPONSEBODY
*/
#define STATE_RESPONSEEND 0x12

/*
Send then response body
Used By: GET

GET: reset target, register for read, proceed to STATE_REQUESTSTART
*/
#define STATE_RESPONSEBODY 0x13

/*
Write a static response, only.
Used By: PUT, DELETE, error cases

ALL: reset target, register for read, proceed to STATE_REQUESTSTART
*/
#define STATE_RESPONSEWRITEONLY 0x14

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
#define REQUEST_IS(type, mask) ((type & mask) == mask)

/* ===[ HTTP TEMPLATES ]=== */
#define NUMBER_OF_HTTPTEMPLATE 7
#define HTTPTEMPLATE_HEADERS200 0
#define HTTPTEMPLATE_FULL404 1
#define HTTPTEMPLATE_FULL200OK 2
#define HTTPTEMPLATE_NEWLINE 3
#define HTTPTEMPLATE_DBLNEWLINE 4
#define HTTPTEMPLATE_FULLINVALIDMETHOD 5
#define HTTPTEMPLATE_FULLHTTP200DELETED 6

static const char http_templates[NUMBER_OF_HTTPTEMPLATE][100] = {
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\n",
	"HTTP/1.1 404 File Not Found\r\nConnection: Keep-Alive\r\nContent-Length: 15\r\n\r\nKey not Found\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 4\r\n\r\nOK\r\n",
	"\r\n",
	"\r\n\r\n",
	"HTTP/1.1 400 Bad Request\r\nConnection: Close\r\nContent-Length: 14\r\nInvalid Method\r\n\r\n",
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 9\r\n\r\nDELETED\r\n"
};

int extern http_templates_length[NUMBER_OF_HTTPTEMPLATE];

/* Parsable headers */
#define HEADER_CONTENTLENGTH 1
#define HEADER_XTTL 2

/* Methods */
bool http_read_handle(int epfd, cache_connection* connection);
bool http_write_handle(int epfd, cache_connection* connection);
void http_templates_init();
void http_connection_handler(cache_connection* connection);
#endif // !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
