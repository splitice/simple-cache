#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "config.h"
#include "debug.h"
#include "scache.h"
#include "hash.h"
#include "http.h"
#include "db.h"

#define IS_SINGLE_FILE(x) x->data_length>BLOCK_LENGTH
#define CONNECTION_HASH_KEY(x) x%CONNECTION_HASH_ENTRIES


#define DBL_LB_UINT32 *(const uint32_t*)(char[4]){'\r','\n','\r','\n'}//Windows
#define DBL_LB_UINT16 *(const uint16_t*)(char[2]){'\n','\n'}//Unix

//Cache Memory
cache_entry cache_hash_set[HASH_ENTRIES] = { 0 };
block_free_node* free_blocks = NULL;

//Paths
char filename_buffer[MAX_PATH];

//Misc
int listenfd;
int stop_soon = 0;
struct epoll_event ev;
char misc_buffer[4096];

void get_key_path(cache_entry* e, char* out){
	char folder1 = 'A' + (e->hash % 26);
	char folder2 = 'A' + ((e->hash >> 8) % 26);
	snprintf(out, MAX_PATH, "%s%c%c/%s/%x", db.path_single, folder1, folder2, e->hash);
}

int open_cache_entry(cache_entry* e){
	get_key_path(e, filename_buffer);
	int fd = open(filename_buffer, O_RDWR);
	if (fd <= 0){
		WARN("Unable to open cache file: %s", filename_buffer);
	}
	return fd;
}

uint32_t hash_string(char* str, int length){
	uint32_t out;
	MurmurHash3_x86_32(str, length, 13, &out);
	return out;
}

cache_entry* get_entry_read(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = &cache_hash_set[hash_key];
	if (entry->key == NULL || entry->key_length != length || strncmp(key, entry->key, length)){
		return NULL;
	}

	return entry;
}

cache_entry* get_entry_write(char* key, size_t length){
	uint32_t hash = hash_string(key, length);

	int hash_key = hash % HASH_ENTRIES;
	cache_entry* entry = &cache_hash_set[hash_key];

	//This is a re-used entry
	if (entry->key != NULL){
		//We have clients reading this key, cant write currently
		if (entry->refs){
			return NULL;
		}

		free(entry->key);
	}

	entry->key = (char*)malloc(sizeof(char) * length);
	memcpy(key, entry->key, sizeof(char)* length);
	entry->key_length = length;
	entry->hash = hash;

	return entry;
}

void entry_write_init(cache_entry* entry, uint32_t data_length){
	if (data_length > BLOCK_LENGTH){
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a file, and the entry is currently a file
			get_key_path(entry, filename_buffer);
			truncate(filename_buffer, data_length);
		}
		else{
			//We are going to use a file, and the entry is currently a block

		}
	}
	else{
		if (IS_SINGLE_FILE(entry)){
			//We are going to store in a block, and the entry is currently a file
			get_key_path(entry, filename_buffer);
			unlink(filename_buffer);
		}
		//Else: We are going to use a block, and the entry is currently a block
	}
}

/*----------------------------------------------------------------------
Portable function to set a socket into nonblocking mode.
Calling this on a socket causes all future read() and write() calls on
that socket to do only as much as they can immediately, and return
without waiting.
If no data can be read or written, they return -1 and set errno
to EAGAIN (or EWOULDBLOCK).
Thanks to Bjorn Reese for this code.
----------------------------------------------------------------------*/
int setNonblocking(int fd)
{
	int flags;

	/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	/* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	/* Otherwise, use the old way of doing it */
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

void open_listener(){
	int res;

	struct sockaddr_in servaddr;
	/* Set up to be a daemon listening on port 8000 */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);


	/* Enable address reuse */
	int on = 1;
	res = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (res < 0){
		goto fail;
	}

	//bind
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(8000);
	res = bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (res < 0){
		goto fail;
	}
	/* Force the network socket into nonblocking mode */
	//res = setNonblocking(listenfd);
	//if (res < 0){
//		goto fail;
	//}


	res = listen(listenfd, 10);
	if (res < 0){
		goto fail;
	}

	return;
fail:
	PFATAL("error opening listener");
}

static void connection_add(int fd, cache_connection_node* ctable){
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	if (node->connection.client_sock != -1){
		while (node->next != NULL) {
			node = node->next;
		}

		cache_connection_node* newNode = (cache_connection_node*)malloc(sizeof(cache_connection_node));
		node->next = newNode;
		node = newNode;
	}
	node->connection.input_read_position = 0;
	node->connection.input_buffer_write_position = 0;
	node->connection.state = STATE_READING_REQUEST_METHOD;
	node->connection.client_sock = fd;
	node->connection.output_buffer_free = NULL;
}

static cache_connection* connection_get(int fd, cache_connection_node* ctable){
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	while (node->connection.client_sock != fd){
		node = node->next;
		if (node == NULL){
			return NULL;
		}
	}

	return &node->connection;
}

static void connection_remove(int fd, cache_connection_node* ctable){
	cache_connection_node* prev = NULL;
	cache_connection_node* node = &ctable[CONNECTION_HASH_KEY(fd)];
	while (node->connection.client_sock != fd){
		prev = node;
		node = node->next;
		if (node == NULL){
			WARN("Unable to find fd: %d connection entry to remove", fd);
			return;
		}
	}

	if (prev){
		//Not the first node in a linked list
		prev->next = node->next;
		free(node);
	}
	else{
		if (node->next){
			//Has nodes after it
			memcpy(&node->next->connection, &node->connection, sizeof(cache_connection));
			prev = node->next;
			node->next = prev->next;
			free(prev);
		}
		else{
			//Is a single entry in table
			node->connection.client_sock = -1;
		}
	}
}

/*
process the client connection (read/write)

returns 1 done
*/
static int process_fd(int fd, int epfd, cache_connection* connection){
	bool done = false;
	cache_target* target = &connection->target;
	if (connection->type == REQTYPE_READ){
		size_t to_read = target->end_position - target->position;
		ssize_t bytes_sent = sendfile(fd, target->fd, &target->position, to_read);
		target->position += bytes_sent;
		if (target->position >= target->end_position){
			done = true;
		}
	}
	else if (connection->type == REQTYPE_WRITE){
		int to_write = connection->input_buffer_write_position - connection->input_read_position;
		ssize_t read_bytes = write(target->fd, connection->input_buffer + connection->input_read_position, to_write);
		connection->input_read_position += read_bytes;

		//TODO: use content-length header
		done = true;
	}
	else{
		FATAL("Unknown connection type %d", connection->type);
	}

	if (done){
		connection->state = STATE_READING_REQUEST_METHOD;
		connection->target.entry->refs--;
		return 1;
	}
	else{
		return 0;
	}
}

static void register_handle_write(int fd, int epfd){
	ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}
}

static void register_handle_read(int fd, int epfd){
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
	ev.data.fd = fd;
	int res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}
}

static int handle_fd(int fd, int epfd, cache_connection* connection){
	int continue_loop = 0;
	char* buffer;
	char* start;
	char* end;

	//Handle
	do {
		continue_loop = 0;
		switch (connection->state){
		case STATE_READING_REQUEST_METHOD:;
			
			break;

		case STATE_READING_REQUEST_KEY:;
			DEBUG("[#%d] Handling STATE_READING_REQUEST_KEY\n", fd);
			start = buffer = (char*)(connection->input_buffer + connection->input_read_position);
			end = (char*)(connection->input_buffer + connection->input_buffer_write_position);

			while (buffer < end){
				if (*buffer == ' '){
					int length = buffer - start - 1;
					*buffer = 0;//Null terminate the key
					DEBUG("[#%d] Request key: \"%s\"\n", fd, start);
					cache_entry* entry;
					if (connection->type == REQTYPE_READ){
						entry = get_entry_read(start, length);
					}
					else{
						entry = get_entry_write(start, length);
					}
					connection->target.entry = entry;
					if (entry != NULL){
						if (IS_SINGLE_FILE(entry)){
							connection->target.fd = open_cache_entry(entry);
							connection->target.end_position = entry->data_length;
						}
						else{
							connection->target.fd = db.fd_blockfile;
							connection->target.position = entry->block * BLOCK_LENGTH;
							connection->target.end_position = connection->target.position + entry->data_length;
						}
						entry->refs++;

						connection->output_buffer = http_responses[HTTPRESPONSE_HEADERS200];
						connection->output_length = http_responses_length[HTTPRESPONSE_HEADERS200];
					}
					else{
						connection->output_buffer = http_responses[HTTPRESPONSE_FULL404];
						connection->output_length = http_responses_length[HTTPRESPONSE_FULL404];
					}
					connection->input_read_position += length;
					connection->state = STATE_READING_REQUEST_HEADERS;
					continue_loop = 1;
					break;
				}
				buffer++;
			}
			break;


		case STATE_READING_REQUEST_HEADERS:;
			
			break;

		case STATE_PROCESS_RESPONSE_BAD:
			DEBUG("[#%d] Handling STATE_PROCESS_RESPONSE_BAD\n", fd);

		case STATE_PROCESS_RESPONSE:;
			DEBUG("[#%d] Handling STATE_PROCESS_RESPONSE\n", fd);
			int num = write(fd, connection->output_buffer, connection->output_length);
			connection->output_length -= num;

			if (connection->output_length == 0){
				connection->output_buffer = NULL;
				if (connection->output_buffer_free){
					free(connection->output_buffer_free);
				}

				if (connection->type == REQTYPE_READ){
					if (connection->target.entry){
						connection->state = STATE_PROCESS_CONTENT;
						continue_loop = 1;
					}
					else{
						connection->state = STATE_READING_REQUEST_METHOD;
						register_handle_read(fd, epfd);
					}
				}
				else
				{
					connection->state = STATE_READING_REQUEST_METHOD;
					register_handle_read(fd, epfd);
				}


				if (connection->state == STATE_PROCESS_RESPONSE_BAD){
					return 2;
				}
			}

			break;

		case STATE_PROCESS_CONTENT:;
			DEBUG("[#%d] Handling STATE_PROCESS\n", fd);
			continue_loop = !process_fd(fd, epfd, connection);
			if (continue_loop && buffer >= end){
				continue_loop = 0;
			}
			else if (!continue_loop){
				//Cleanup target
				if (connection->target.fd != -1){
					//No more references, clean up file handle
					if (!connection->target.entry->refs){
						close(connection->target.fd);
						connection->target.fd = -1;
					}
				}

				//If we have just received all the written data, respond
				if (connection->type == REQTYPE_WRITE)
				{
					connection->output_buffer = http_responses[HTTPRESPONSE_FULL200OK];
					connection->output_length = http_responses_length[HTTPRESPONSE_FULL200OK];
					connection->state = STATE_PROCESS_RESPONSE;
					register_handle_write(fd, epfd);
				}
				else{
					register_handle_read(fd, epfd);
				}
			}
			break;

		case STATE_PROCESS_ENTRY_CONTENTLENGTH:
			DEBUG("[#%d] Handling STATE_PROCESS_ENTRY_CONTENTLENGTH\n", fd);
			int chars = snprintf(misc_buffer, 4096, "Content-Length: %d\r\n", connection->target.entry->data_length);

			char* content_length = malloc(chars * sizeof(char));
			memcpy(content_length, misc_buffer, chars);

			connection->output_buffer = content_length;
			connection->output_length = chars;
			connection->output_buffer_free = chars;

			break;
		case STATE_PROCESS_ENTRY_DBLNEWLINE:
			DEBUG("[#%d] Handling STATE_PROCESS_ENTRY_DBLNEWLINE\n", fd);
			connection->output_buffer = http_responses[HTTPRESPONSE_DBLNEWLINE];
			connection->output_length = http_responses_length[HTTPRESPONSE_DBLNEWLINE];
			connection->state = STATE_PROCESS_CONTENT;
			register_handle_write(fd, epfd);

			break;
		}
	} while (continue_loop);

	return 0;
}

static void epoll_event_loop(void){
	int epfd = epoll_create(MAXCLIENTS);

	struct epoll_event events[5];
	//add api fd
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	ev.data.fd = listenfd;
	int res = epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
	if (res != 0){
		PFATAL("epoll_ctl() failed.");
	}

	struct cache_connection_node ctable[CONNECTION_HASH_ENTRIES] = { 0 };
	for (int i = 0; i < CONNECTION_HASH_ENTRIES; i++){
		ctable[i].connection.client_sock = -1;
	}

	while (!stop_soon) {
		int nfds = epoll_wait(epfd, events, 5, -1);
		int n = 0;
		while (n < nfds) {
			int fd = events[n].data.fd;
			if (fd == listenfd){
				if (events[n].events & EPOLLIN){
					DEBUG("[#] Accepting connection\n");
					int client_sock = accept(listenfd, NULL, NULL);

					if (client_sock < 0) {
						WARN("Unable to handle API connection: accept() fails. Error: %s", strerror(errno));
					}
					else {
						DEBUG("[#] Accepted connection %d\n", client_sock);

						if (fcntl(client_sock, F_SETFL, O_NONBLOCK))
							PFATAL("fcntl() to set O_NONBLOCK on API connection fails.");

						ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
						ev.data.fd = client_sock;
						res = epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &ev);
						if (res != 0){
							PFATAL("epoll_ctl() failed.");
						}

						connection_add(client_sock, ctable);
					}
				}
				else if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP){
					FATAL("API socket is down.");
				}
			}
			else{
				DEBUG("[#%d] Got socket event %d\n", fd, events[n].events);
				cache_connection* connection = connection_get(fd, ctable);
				if (connection != NULL){
					int close_connection = 0;

					if (events[n].events & EPOLLERR || events[n].events & EPOLLHUP || events[n].events & EPOLLRDHUP){
						close_connection = 1;
					}
					else if (events[n].events & EPOLLIN){
						if (handle_fd_read(fd, epfd, connection)){
							close_connection = 1;
						}
					}
					else if (events[n].events & EPOLLOUT){
						if (handle_fd(fd, epfd, connection)){
							close_connection = 1;
						}
					}

					if (close_connection){
						connection_remove(fd, ctable);
						close(fd);
					}
				}
				else{
					WARN("Unknown connection %d", fd);
					close(fd);
				}
			}
			n++;
		}
	}

	close(epfd);
}

/* Time to go down the rabbit hole */
int main()
{
	http_templates_init();
	db_open("/dbtest");
	open_listener();
	epoll_event_loop();
}