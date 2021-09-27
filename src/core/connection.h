#if !defined(CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "config.h"
#include "connection_structures.h"

bool connection_register_write(struct scache_connection* c);
bool connection_register_read(struct scache_connection* c);
int connection_open_listener(struct scache_bind bind);
void connection_close_listeners();
void connection_event_loop(void(*connection_handler)(scache_connection* connection), int monitoring_fd);
void connection_setup(struct scache_binds cache_binds, struct scache_binds cache_monitor);
void connection_cleanup();
bool connection_remove(int fd);
bool connection_stop_soon();


void close_fd(int fd);

#define CONNECTION_HANDLER_ACTUAL(con, value) (con)->handler = (value) 
#define _DEBUG_CONNECTION_HANDLER
#ifdef DEBUG_CONNECTION_HANDLER
#define CONNECTION_HANDLER(con, value) printf("Setting connection handler to " #value "\n"); (con)->handler_name=#value;CONNECTION_HANDLER_ACTUAL(con, value)
#else
#define CONNECTION_HANDLER(con, value) (con)->handler_name=#value;CONNECTION_HANDLER_ACTUAL(con, value)
#endif

#define CONNECTION_HASH_KEY(x) (x)%CONNECTION_HASH_ENTRIES

#endif // !defined(CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
