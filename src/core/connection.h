#if !defined(CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "config.h"
#include "connection_structures.h"

bool connection_register_write(int epfd, int fd);
bool connection_register_read(int epfd, int fd);
int connection_open_listener(struct scache_bind bind);
void connection_close_listener();
void connection_event_loop(void(*connection_handler)(cache_connection* connection));
void connection_setup(struct scache_bind* binds, uint32_t num_binds);
void connection_cleanup();

#define CONNECTION_HASH_KEY(x) x%CONNECTION_HASH_ENTRIES

#endif // !defined(CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
