#include "connection.h"

#define http_respond_start http_respond_expires
state_action http_respond_expires(scache_connection* connection);
state_action http_respond_contentlength(scache_connection* connection);
state_action http_respond_responseend(scache_connection* connection);
state_action http_respond_contentbody(scache_connection* connection);
state_action http_respond_writeonly(scache_connection* connection);
state_action http_respond_cleanupafterwrite(scache_connection* connection) ;
state_action http_respond_listing(scache_connection* connection);
state_action http_respond_listingentries(scache_connection* connection);
state_action http_respond_stats(scache_connection* connection);