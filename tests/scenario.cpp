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
#include <arpa/inet.h>
#include <sys/time.h>
#include <map>
#include "debug.h"

#define UNIT_REQUEST ">>>>>"
#define UNIT_RESPONSE "-----"
#define UNIT_SEPERATOR_LEN 6

bool extract_unit(FILE* f, std::string& request, std::string& expect, int& connection){
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	int state = 0;
	long last_pos;
	connection = 0;
	while ((read = getline(&line, &len, f)) != -1) {
		if (read >= 2){
			if (line[read - 2] == '\r'){
				line[read - 2] = '\n';
				line[read - 1] = '\0';
				read--;
			}
		}
		switch (state){
		case 0:
			if (read == UNIT_SEPERATOR_LEN){
				if (strncmp(line, UNIT_REQUEST, 5) == 0){
					state++;
				}
			}
			else{
				char* buf = line;
				int remlen = read;
				while (isdigit(*buf)){
					remlen--;

					if (remlen == UNIT_SEPERATOR_LEN){
						if (strncmp(buf, UNIT_REQUEST, 5) == 0){
							*buf = 0;
							connection = atoi(line);
							state++;
						}
					}
					else if (remlen < UNIT_SEPERATOR_LEN){
						break;
					}
				}
			}
			break;
		case 1:
			if (read == UNIT_SEPERATOR_LEN){
				if (strncmp(line, UNIT_RESPONSE, 5) == 0){
					state++;
					break;
				}
			}
			request += line;
			break;
		case 2:
			if (read == UNIT_SEPERATOR_LEN){
				if (strncmp(line, UNIT_REQUEST, 5) == 0){
					fseek(f, last_pos, SEEK_SET);
					free(line);
					return true;
				}
			}
			last_pos = ftell(f);
			expect += line;
			break;
		}
	}

	free(line);
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

	tv.tv_sec = 3;  /* 1 Sec Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

	return sockfd;
}

bool run_unit(std::string& request, std::string& expect, int sockfd){
	char recv_buffer[1025];

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
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				printf("A timeout occured waiting for a response\n");
				return false;
			}
			PFATAL("An error occured reading from socket");
		}

		int n = remove_cr(recv_buffer, received);

		if (n == 0){
			printf("Expected: %s\n", buffer);
			printf("Connection Closed\n");
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
	}

	return true;
}

pid_t start_server(const char* binary_path, int port, const char* db){
	char execcmd[512];

	char* pidfile = tempnam(NULL, NULL);

	if (access(binary_path, X_OK)){
		WARN("%s not executable or does not exist", binary_path);
		return -1;
	}

	sprintf(execcmd, "%s -d -o --bind-port %d --database-file-path %s --make-pid %s", binary_path, port, db, pidfile);
	system(execcmd);

	FILE* f;
	do {
		f = fopen(pidfile, "r");
		usleep(100);
	} while (f == 0);
	char* line = NULL;
	size_t len;
	getline(&line, &len, f);
	int pid = atoi(line);
	fclose(f);
	free(pidfile);

	return pid;
}

void stop_server(pid_t pid){
	int res = kill(pid, SIGTERM);
	if (res != 0){
		PFATAL("Unable to kill scache service");
	}
	sleep(1);
}

void trim_last_nl(std::string* str){
	int length = str->length();
	if ((*str)[length - 1] == '\n'){
		if ((*str)[length - 2] == '\r'){
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
	std::map<int, int> connections;

	int step = 1;
	do {
		more = extract_unit(f, request, expect, connection);

		if (request.empty() || expect.empty()){
			fclose(f);
			return false;
		}

		//Remove last newline
		trim_last_nl(&request);
		trim_last_nl(&expect);

		if (connections.find(connection) == connections.end()){
			connections[connection] = unit_connect(port);
		}

		int sockfd = connections[connection];

		bool result = run_unit(request, expect, sockfd);
		if (!result){
			fclose(f);
			printf("Failed to run step %d of file %s\n", step, filename);
			return false;
		}

		if (more){
			request.clear();
			expect.clear();
			step++;
		}
	} while (more);

	fclose(f);

	return true;
}

bool run_scenario(const char* binary, const char* testcases, const char* filename, int port, bool run_server){
	char testcase_path[1024];
	sprintf(testcase_path,"%s/%s", testcases, filename);
	char* db = tempnam(NULL, NULL);
	int res = mkdir(db, 0777);
	if (res < 0){
		PFATAL("Failed to create temporary directory: %s", db);
	}
	pid_t pid;
	if (run_server){
		pid = start_server(binary, port, db);
		if (pid < 0){
			WARN("Failed to start simple-cache server");
			stop_server(pid);
			return true;
		}
	}
	bool result = execute_file(testcase_path, port);
	if (run_server){
		stop_server(pid);
	}

	//Cleanup temporary directory
	sprintf(testcase_path, "rm -Rf \"%s\"", db);
	system(testcase_path);

	return result;
}

bool run_scenarios(const char* binary, const char* testcases, const char* directory_path, int port, bool run_server){
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
		bool result = run_scenario(binary, directory_buffer, next_file->d_name, port, run_server);
		if (!result){
			SAYF("Scenario %s failed\n", next_file->d_name);
			full_result &= result;
		}
	}
	return full_result;
}