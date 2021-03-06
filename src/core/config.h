#define MAXCLIENTS 10240
#define BLOCK_LENGTH 4096
#define BLOCK_MAX_LOAD 8192
#define MAX_PATH 512
#define SHORT_PATH 400
#define NUM_EVENTS 32
#define NUM_EVENTS_ACCEPT 8

#define CONNECTION_HASH_ENTRIES 4096

#ifdef DEBUG_BUILD
#define DB_LRU_EVERY 1
#else
#define DB_LRU_EVERY 50
#endif

#define DEFAULT_LISTING_LIMIT 10000
#define HASH_SEED 13

#define _FILE_OFFSET_BITS 64
#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE 1
#endif
