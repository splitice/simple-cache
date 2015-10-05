#include "connection.h"

state_action http_handle_method(int epfd, cache_connection* connection);
state_action http_handle_url(int epfd, cache_connection* connection);
state_action http_handle_httpversion(int epfd, cache_connection* connection);
state_action http_handle_eolwrite(int epfd, cache_connection* connection);
state_action http_handle_headers_extract(int epfd, cache_connection* connection);
state_action http_handle_headers(int epfd, cache_connection* connection);
state_action http_handle_request_body(int epfd, cache_connection* connection);
state_action http_handle_eolstats(int epfd, cache_connection* connection);