#include <stdio.h>
#include <dirent.h>
#include <string>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <map>
#include "debug.h"

#define UNIT_REQUEST ">>>>>"
#define UNIT_EXPECT_RESPONSE "-----"
#define UNIT_EXPECT_CLOSE "----c"
#define UNIT_DELAY "*****"
#define UNIT_SEPERATOR_LEN 6

#define STATE_HEADER 0
#define STATE_REQUEST 1
#define STATE_RESPONSE 2
#define STATE_EXPECT_CLOSE 3

enum unit_command{
	none = 0,
	close_command,
	shutdown_read_command,
	shutdown_write_command,
	shutdown_duplex_command,
	expect_close
};

bool extract_unit(FILE* f, std::string& request, std::string& expect, int& connection, unit_command& command){
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	int state = STATE_HEADER;
	long last_pos = -1;
	connection = 0;
	command = none;
	while ((read = getline(&line, &len, f)) != -1) {
		int expect_len = UNIT_SEPERATOR_LEN;
		if (read >= 2){
			if (line[read - 2] == '\r'){
				line[read - 2] = '\n';
				line[read - 1] = '\0';
				read--;
			}
		}
		if (read >= 1 && line[read - 1] != '\n'){
			expect_len--;
		}
		switch (state){
		case STATE_HEADER:
			if (read == expect_len){
				if (strncmp(line, UNIT_REQUEST, 5) == 0){
					state++;
				}
			}
			else{
				char* buf = line;
				int remlen = read;
				while (remlen >= expect_len && isdigit(*buf)){
					remlen--;
					buf++;

					if (remlen == expect_len){
						if (strncmp(buf, UNIT_REQUEST, 5) == 0){
							*buf = 0;
							connection = atoi(line);
							state++;
						}
					}
				}
			}
			break;
		case STATE_REQUEST:
			last_pos = ftell(f);
			if (read == UNIT_SEPERATOR_LEN){
				if (strncmp(line, UNIT_EXPECT_RESPONSE, 5) == 0){
					state = STATE_RESPONSE;
					break;
				}
				if (strncmp(line, UNIT_EXPECT_CLOSE, 5) == 0){
					state = STATE_EXPECT_CLOSE;
					break;
				}
			}
			request += line;
			break;
		case STATE_EXPECT_CLOSE:
			command = expect_close;
			free(line);
			break;
		case STATE_RESPONSE:
			//printf("TravisCI (%d): %s\n", read, line);
			if (read == expect_len){
				if (strncmp(line, UNIT_REQUEST, 5) == 0){
					fseek(f, last_pos, SEEK_SET);
					free(line);
					return true;
				}
			}
			else{
				if (*line == 'c'){
					if (strncmp(line+1, UNIT_DELAY, 5) == 0){
						printf("Close connection after step: %c\n", *line);
						command = close_command;
						free(line);
						line = NULL;
					}
				} else if (*line == 's'){
					line++;
					if(*line == 'r'){
						command = shutdown_read_command;
					} else if(*line == 'w') {
						command = shutdown_write_command;
					} else{
						command = shutdown_duplex_command;
					}
					line --;
					if (strncmp(line+2, UNIT_DELAY, 5) == 0){
						printf("End connection after step: %c\n", *line);
						free(line);
						line = NULL;
					}else{
						command = none;
					}
				} else{
					char* buf = line;
					int remlen = read;
					while (remlen >= expect_len && isdigit(*buf)){
						remlen--;
						buf++;

						if (remlen == expect_len){
							if (strncmp(buf, UNIT_REQUEST, 5) == 0){
								fseek(f, last_pos, SEEK_SET);
								free(line);
								return true;
							}
							if (strncmp(buf, UNIT_DELAY, 5) == 0){
								*buf = 0;
								printf("Sleeping for %s seconds\n", line);
								sleep(atoi(line));
							}
						}
					}
					free(line);
					line = NULL;
				}
			}
			last_pos = ftell(f);
			if (line){
				expect += line;
			}
			break;
		}
	}

	if(line != NULL) free(line);
	if (state == 1){
		printf("Error parsing scenario (possibly incomplete)\n");
		request.clear();
		expect.clear();
		return false;
	}
	
	return false;
}

pid_t system2(const char * command)
{
	pid_t pid;

	pid = fork();

	if (pid < 0) {
		return pid;
	}
	else if (pid == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", command, NULL);
		_exit(1);
	}

	return pid;
}

int remove_cr(char* buffer, int n){
	int ret = n;
	int writePos = 0;
	for (int i = 0; i < n; i++){
		if (buffer[i] == '\r'){
			ret--;
		}
		else{
			if (i != writePos){
				buffer[writePos] = buffer[i];
			}
			writePos++;
		}
	}
	return ret;
}

int unit_connect(int port){
	int sockfd;
	struct sockaddr_in servaddr, cliaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(port);

	int res;

	struct timeval start_time;
	int err = gettimeofday(&start_time, NULL);

	struct timeval current_time;
	do {
		res = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
		if (res < 0){
			usleep(100);
		}
		int err = gettimeofday(&current_time, NULL);
		if (err == -1){
			PFATAL("Failed to get system time");
		}
	} while (res < 0 && (current_time.tv_sec - start_time.tv_sec) < 3);

	if (res < 0){
		PFATAL("Failed to connect to scache server\n");
	}

	struct timeval tv;

	tv.tv_sec = 3;  /* 3 Sec Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

	return sockfd;
}

bool run_unit(std::string& request, std::string& expect, int sockfd){
	char recv_buffer[8096];
	int one = 1;
	int received_total = 0;

	// NO DELAY so we can form packets
	setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

	//Send request
	int len = request.length();
	const char* buffer = request.c_str();
	while (len != 0){
		int sent = send(sockfd, buffer, len, 0);

		buffer += sent;
		len -= sent;
	}

	//Receive response
	buffer = expect.c_str();
	len = expect.length();

	while (len != 0){
		int to_recv = sizeof(recv_buffer)-1;
		if (len < to_recv){
			to_recv = len;
		}
		int received = recv(sockfd, recv_buffer, to_recv, 0);

		if (received == -1){
			printf("Request:\n");
			printf("=========================================\n");
			printf("%s\n", request.c_str());
			printf("=========================================\n");
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				printf("A timeout occured waiting for a response of length %d (had received %d of %d)\n", to_recv, received_total, len);
				return false;
			}
			PFATAL("An error occured reading from socket");
		}

		int n = remove_cr(recv_buffer, received);

		if (n == 0){
			printf("Expected: %s\n", buffer);
			printf("Got: Connection Closed\n");
			return false;
		}

		if (strncmp(recv_buffer, buffer, n) != 0){
			int read_length = buffer - expect.c_str();
			*(recv_buffer + n) = 0;//Incase we arent comparing it all
			if (expect.length() < (n + read_length)){
				printf("Expected (insufficient bytes): %s\n", expect.c_str());
			}
			else{
				printf("Expected: %s\n", expect.substr(read_length, n).c_str());
			}
			printf("Got (length: %d): %s\n", n, recv_buffer);
			return false;
		}

		len -= n;
		buffer += n;
		received_total += n;
	}

	return true;
}

pid_t start_server(const char* binary_path, int port, const char* db, const char* options, bool debug_output = true){
	char execcmd[512];
	int res;

	char* pidfile = tempnam(NULL, NULL);

	if (access(binary_path, X_OK)){
		WARN("%s not executable or does not exist", binary_path);
		return -1;
	}

	sprintf(execcmd, "%s -o --bind 0.0.0.0:%d --database-file-path %s --leave-pid --make-pid %s %s %s &", binary_path, port, db, pidfile, options, debug_output?"":"2>/dev/null");
	printf("Executing: %s", execcmd);
	res = system(execcmd);
	if (res < 0){
		puts("\n");
		PFATAL("Unable to execute scache server");
	}

	FILE* f = NULL;
	res = 0;
	do {
		if (!access(pidfile, F_OK)){
			f = fopen(pidfile, "r");
		}
		usleep(100000);
	} while (f == NULL && res++ < 100);
	free(pidfile);

	if(f == NULL){
		FATAL("Failed to find PID file");
	}

	char* line = NULL;
	size_t len;
	res = getline(&line, &len, f);
	fclose(f);

	if (res < 0){
		free(line);
		puts("\n");
		PFATAL("Unable to read PID file");
	}

	int pid = atoi(line);
	free(line);

	printf(" (PID: %d)\n", pid);

	return pid;
}

void stop_server(pid_t pid){
	if (kill(pid, 0) != 0)
	{
		WARN("PID %d did not exist before stopping server\n", pid);
	}

	int res = kill(pid, SIGTERM);
	if (res != 0){
		PFATAL("Unable to kill scache service %d", pid);
	}
	
	while (kill(pid, 0) == 0 || errno != ESRCH)
	{
		usleep(100);
	}
}

void trim_last_nl(std::string* str){
	int length = str->length();
	if (length >= 1 && (*str)[length - 1] == '\n'){
		if (length >= 2 && (*str)[length - 2] == '\r'){
			*str = str->substr(0, length - 2);
		}
		else{
			*str = str->substr(0, length - 1);
		}
	}
}

bool execute_file(const char* filename, int port){
	FILE* f = fopen(filename, "r");

	bool more;
	std::string request;
	std::string expect;
	int connection;
	unit_command cmd = none;
	std::map<int, int> connections;

	int step = 1;
	do {
		printf("Running scenarios \"%s\" step %d\n", filename, step);
		more = extract_unit(f, request, expect, connection, cmd);

		if (request.empty() && expect.empty()){
			fclose(f);
			return false;
		}

		//Remove last newline
		trim_last_nl(&request);
		trim_last_nl(&expect);

		//Lookup connection
		if (connections.find(connection) == connections.end()){
			connections[connection] = unit_connect(port);
		}

		//Reconnect if disconnected
		int error = 0;
		socklen_t len = sizeof (error);
		int retval = getsockopt(connections[connection], SOL_SOCKET, SO_ERROR, &error, &len);
		if (retval != 0){
			connections[connection] = unit_connect(port);
		}

		int sockfd = connections[connection];

		//Execute the step
		bool result = run_unit(request, expect, sockfd);
		if (!result){
			fclose(f);
			printf("Failed to run step %d of file %s\n", step, filename);
			return false;
		}

		//Close connection if asked
		if (cmd == close_command)
		{
			close(connections[connection]);
			connections.erase(connections.find(connection));
		}
		else if (cmd == shutdown_read_command)
		{
			shutdown(connections[connection], SHUT_RD);
		}
		else if (cmd == shutdown_write_command)
		{
			shutdown(connections[connection], SHUT_WR);
		}
		else if (cmd == shutdown_duplex_command)
		{
			shutdown(connections[connection], SHUT_RDWR);
		}

		//Are there more tests
		if (more){
			request.clear();
			expect.clear();
			step++;
		}
	} while (more);

	fclose(f);

	return true;
}

static bool check_port(int port){
	int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
	bool ret;	
    const char *hostname = "127.0.0.1";

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        PFATAL("ERROR opening socket");
    }

    server = gethostbyname(hostname);

    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);

    serv_addr.sin_port = htons(port);
    ret = connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) >= 0;

    close(sockfd);

	return ret;
}

bool run_scenario(const char* binary, const char* testcases, const char* filename, int port, bool run_server, const char* options, bool debug_output = true){
	printf("Running scenarios \"%s\"\n", testcases);
	char testcase_path[1024];
	sprintf(testcase_path,"%s/%s", testcases, filename);
	char* db = tempnam(NULL, NULL);
	int res = mkdir(db, 0777);
	bool result;
	if (res < 0){
		free(db);
		PFATAL("Failed to create temporary directory: %s", db);
	}
	pid_t pid;
	if (run_server){
		// start the scache executable
		pid = start_server(binary, port, db, options, debug_output);
		if (pid < 0){
			WARN("Failed to start simple-cache server");
			stop_server(pid);
			result = true;
			goto end;
		}

		// wait on port available
		while(!check_port(port)){
			usleep(100);
		}
	}
	result = execute_file(testcase_path, port);
	if (run_server){
		stop_server(pid);
	}

	//Cleanup temporary directory
	sprintf(testcase_path, "rm -Rf \"%s\"", db);
	res = system(testcase_path);
	if (res < 0){
		free(db);
		PFATAL("Failed to clean up temporary directory: %s", db);
	}

end:
	free(db);
	return result;
}

bool run_scenarios(const char* binary, const char* testcases, const char* directory_path, int port, bool run_server, const char* options, bool debug_output = true){
	bool full_result = true;
	char directory_buffer[MAX_PATH];
	sprintf(directory_buffer, "%s/%s", directory_path, testcases);
	struct dirent *next_file;
	DIR *theFolder = opendir(directory_buffer);
	if (theFolder == NULL){
		FATAL("%s does not exist", directory_buffer);
	}
	while (next_file = readdir(theFolder))
	{
		if (*(next_file->d_name) == '.')
			continue;
		// build the full path for each file in the folder
		bool result = run_scenario(binary, directory_buffer, next_file->d_name, port, run_server, options, debug_output);
		if (!result){
			SAYF("Scenario %s failed\n", next_file->d_name);
			full_result &= result;
		}
	}
	closedir(theFolder);
	return full_result;
}