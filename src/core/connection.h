#if !defined(CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
#define CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D

#include "config.h"
#include "connection_structures.h"

void connection_register_write(int epfd, int fd);
void connection_register_read(int epfd, int fd);
void connection_open_listener();
void connection_close_listener();
void epoll_event_loop();

#define CONNECTION_HASH_KEY(x) x%CONNECTION_HASH_ENTRIES

#endif // !defined(CONNECTION_H_INCLUDED_0986159D_B42F_44F7_AC22_75D7DDA2994D)
