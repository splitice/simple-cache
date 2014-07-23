#include "connection.h"

#define http_respond_start http_respond_contentlength
bool http_respond_contentlength(int epfd, cache_connection* connection);
bool http_respond_responseend(int epfd, cache_connection* connection);
bool http_respond_contentbody(int epfd, cache_connection* connection);
bool http_respond_writeonly(int epfd, cache_connection* connection);