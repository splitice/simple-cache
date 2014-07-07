#if !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
#define HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8

#include "scache.h"

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
#define STATE_REQUESTSTARTURL 0x2

/*
Extract needed information from headers
Format: $HEADER: $VALUE
Used By: GET,PUT

PUT: register for write, proceed to STATE_RESPONSESTART
*/
#define STATE_REQUESTHEADERS 0x3

/*
Search for the end of the request, skipping over any other headers.
Lazy, high performance request skip state
Format: Search for \r\n\r\n or \n\n
Used By: DELETE

GET: register for write, proceed to STATE_RESPONSEWRITEONLY
DELETE: register for write, proceed to STATE_RESPONSEWRITEONLY
*/
#define STATE_REQUESTENDSEARCH 0x5

/*
Handle the request body content (uploaded data)
Format: Handle $CONTENT_LENGTH worth of data
Used By: PUT

PUT: register for write, proceed to STATE_RESPONSEWRITEONLY
*/
#define STATE_REQUESTBODY 0x6

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

/* ===[ METHODS ]=== */
#define REQMETHOD_GET 1
#define REQMETHOD_PUT 2

/* ===[ HTTP TEMPLATES ]=== */
#define NUMBER_OF_HTTPTEMPLATE 5
#define HTTPTEMPLATE_HEADERS200 0
#define HTTPTEMPLATE_FULL404 1
#define HTTPTEMPLATE_FULL200OK 2
#define HTTPTEMPLATE_DBLNEWLINE 3
#define HTTPTEMPLATE_FULLINVALIDMETHOD 4

extern const char http_templates[NUMBER_OF_HTTPTEMPLATE][100];

int http_templates_length[NUMBER_OF_HTTPTEMPLATE];

/* Parsable headers */
#define HEADER_CONTENTLENGTH 1

/* Methods */
bool http_handle_read(int epfd, cache_connection* connection);
bool http_handle_write(int epfd, cache_connection* connection);
void http_templates_init();
#endif // !defined(HTTP_H_INCLUDED_F90F71F0_0C8E_44EB_9C1D_60CBA7163EC8)
