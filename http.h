/* ===[ READ ]=== */
/*
Parse Request line
Format: $METHOD $PATH HTTP/$VERSION
Used By: GET, PUT

GET: proceed to STATE_REQUESTENDSEARCH
PUT: proceed to STATE_REQUESTHEADERS
DELETE: proceed to STATE_REQUESTENDSEARCH
INVALID: register for write, proceed to STATE_RESPONSEWRITEONLY
*/
#define STATE_REQUESTSTART 0x1

/*
Extract needed information from headers
Format: $HEADER: $VALUE
Used By: GET,PUT

PUT: proceed to STATE_REQUESTEND
*/
#define STATE_REQUESTHEADERS 0x2

/*
Handle the end of a request.
Format: None
Used By: GET,PUT

PUT: proceed to STATE_REQUESTBODY
*/
#define STATE_REQUESTEND 0x3

/*
Search for the end of the request, skipping over any other headers
Format: Search for \r\n\r\n or \n\n
Used By: DELETE

GET: register for write, proceed to STATE_RESPONSEWRITEONLY
DELETE: register for write, proceed to STATE_RESPONSEWRITEONLY
*/
#define STATE_REQUESTENDSEARCH 0x4

/*
Handle the request body content (uploaded data)
Format: Handle $CONTENT_LENGTH worth of data
Used By: PUT

PUT: register for write, proceed to STATE_RESPONSEWRITEONLY
*/
#define STATE_REQUESTBODY 0x5

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

char http_templates[NUMBER_OF_HTTPTEMPLATE][100] = {
	"HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\n",
	"HTTP/1.1 404 File Not Found\r\nnConnection: Keep-Alive\r\nContent-Length: 4\r\n404-\r\n\r\n",
	"HTTP/1.1 200 OK\r\nnConnection: Keep-Alive\r\nContent-Length: 2\r\nOK\r\n\r\n",
	"\r\n\r\n",
	"HTTP/1.1 400 Bad Request\r\nnConnection: Close\r\nContent-Length: 14\r\nInvalid Method\r\n\r\n",
};

int http_templates_length[NUMBER_OF_HTTPTEMPLATE];


#define HEADER_CONTENTLENGTH 1

/* Methods */
bool http_handle_read(cache_connection* connection);
bool http_handle_write(cache_connection* connection);
void http_templates_init();