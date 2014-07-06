#include "config.h"

void connection_register_write(int epfd, int fd);
void connection_register_read(int epfd, int fd);
static void epoll_event_loop(void);

#define CONNECTION_HASH_KEY(x) x%CONNECTION_HASH_ENTRIES