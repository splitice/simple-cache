#define MAXCLIENTS 10240
#define BLOCK_LENGTH 4096
#define MAX_PATH 512
#define NUM_EVENTS 32

#define CONNECTION_HASH_ENTRIES 1024

#ifdef DEBUG_BUILD
#define DB_LRU_EVERY 1
#else
#define DB_LRU_EVERY 50
#endif

#define DEFAULT_LISTING_LIMIT 10000
#define HASH_SEED 13