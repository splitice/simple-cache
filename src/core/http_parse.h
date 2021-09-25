#include "connection.h"

/* HTTP methods safe for either */
state_action http_read_eoltoend(scache_connection* connection, char* buffer, int n, uint32_t& temporary);
state_action http_handle_eolwritetoend(scache_connection* connection);
state_action http_discard_input(scache_connection* connection);

/* Cache Only */
state_action http_cache_handle_method(scache_connection* connection);
state_action http_cache_handle_url(scache_connection* connection);
state_action http_cache_handle_httpversion(scache_connection* connection);
state_action http_cache_handle_headers_extract(scache_connection* connection);
state_action http_cache_handle_headers(scache_connection* connection);
state_action http_cache_handle_request_body(scache_connection* connection);
state_action http_cache_handle_eolstats(scache_connection* connection);

/* Monitoring Only */
state_action http_mon_handle_headers(scache_connection* connection);
state_action http_mon_handle_url(scache_connection* connection);
state_action http_mon_handle_method(scache_connection* connection);
state_action http_mon_handle_start(scache_connection* connection);

/* Supporting */
void skip_over_newlines(struct read_buffer* rb);

/* Monitoring functions */
void monitoring_add(scache_connection* conn);
void monitoring_destroy(scache_connection* conn);
void monitoring_close();

/* Cache functions */
void cache_destroy(scache_connection* connection);