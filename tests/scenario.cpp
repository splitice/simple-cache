#include <stdio.h>
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
#include "debug.h"

#define UNIT_REQUEST ">>>>>"
#define UNIT_RESPONSE "-----"
#define UNIT_SEPERATOR_LEN 7

bool extract_unit(FILE* f, std::string& request, std::string& expect){
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	int state = 0;
	long last_pos;
	while ((read = getline(&line, &len, f)) != -1) {
		switch (state){
		case 0:
			if (read == UNIT_SEPERATOR_LEN){
				if (strncmp(line, UNIT_REQUEST, 5) == 0){
					state++;
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
					return true;
				}
			}
			last_pos = ftell(f);
			expect += line;
			break;
		}
	}

	if (state < 2){
		printf("Error parsing scenario (possibly incomplete)\n");
		return false;
	}
	
	return false;
}

pid_t system2(const char * command, int * infp, int * outfp)
{
	int p_stdin[2];
	int p_stdout[2];
	pid_t pid;

	if (pipe(p_stdin) == -1)
		return -1;

	if (pipe(p_stdout) == -1) {
		close(p_stdin[0]);
		close(p_stdin[1]);
		return -1;
	}

	pid = fork();

	if (pid < 0) {
		close(p_stdin[0]);
		close(p_stdin[1]);
		close(p_stdout[0]);
		close(p_stdout[1]);
		return pid;
	}
	else if (pid == 0) {
		close(p_stdin[1]);
		dup2(p_stdin[0], 0);
		close(p_stdout[0]);
		dup2(p_stdout[1], 1);
		dup2(open("/dev/null", O_RDONLY), 2);
		/// Close all other descriptors for the safety sake.
		for (int i = 3; i < 4096; ++i)
			close(i);

		setsid();
		execl("/bin/sh", "sh", "-c", command, NULL);
		_exit(1);
	}

	close(p_stdin[0]);
	close(p_stdout[1]);

	if (infp == NULL) {
		close(p_stdin[1]);
	}
	else {
		*infp = p_stdin[1];
	}

	if (outfp == NULL) {
		close(p_stdout[0]);
	}
	else {
		*outfp = p_stdout[0];
	}

	return pid;
}

bool run_unit(std::string& request, std::string& expect, int port){
	int sockfd, n;
	struct sockaddr_in servaddr, cliaddr;
	char recv_buffer[1024];

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(port);

	int res = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	if (res < 0){
		PFATAL("Failed to connect to scache server\n");
	}

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

	struct timeval tv;

	tv.tv_sec = 1;  /* 1 Sec Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

	while (len != 0){
		int to_recv = 1024;
		if (len < to_recv){
			to_recv = len;
		}
		int n = recv(sockfd, recv_buffer, to_recv, 0);

		if (n == -1){
			if (n == EAGAIN || n == EWOULDBLOCK){
				printf("A timeout occured waiting for a response\n");
				return false;
			}
			PFATAL("An error occured reading from socket");
		}

		if (strncmp(recv_buffer, buffer, n) != 0){
			*(buffer + n) == 0;//Incase we arent comparing it all
			*(recv_buffer + n) == 0;//Incase we arent comparing it all
			printf("Expected: %s\n", buffer);
			printf("Got: %s\n", recv_buffer);
			return false;
		}

		len -= n;
		buffer += n;
	}

	return true;
}

pid_t start_server(const char* binary_path, int port, const char* db){
	char execcmd[512];

	if (access(binary_path, X_OK)){
		WARN("%s not executable or does not exist", binary_path);
		return -1;
	}

	sprintf(execcmd,"%s --bind-port %d --database-file-path %s", binary_path, port, db);
	int infp, outfp;
	int pid = system2(execcmd, &infp, &outfp);
	return pid;
}

void stop_server(pid_t pid){
	if (kill(pid, SIGTERM) == -1){
		PFATAL("Unable to kill scache service");
	}
}

bool execute_file(const char* filename, int port){
	FILE* f = fopen(filename, "r");

	bool more;
	std::string request;
	std::string expect;
	do {
		more = extract_unit(f, request, expect);

		bool result = run_unit(request, expect, port);
		if (!result){
			printf("Failed to run step...\n");
			return false;
		}

		if (more){
			request.clear();
			expect.clear();
		}
	} while (more);

	fclose(f);

	return true;
}

bool run_scenario(const char* binary, const char* testcases, const char* filename, int port){
	char testcase_path[1024];
	sprintf(testcase_path,"%s/%s", testcases, filename);
	char* db = tempnam(NULL, NULL);
	int res = mkdir(db, 0777);
	if (res < 0){
		PFATAL("Failed to create temporary directory: %s", db);
	}
	pid_t pid = start_server(binary, port, db);
	if (pid < 0){
		WARN("Failed to start simple-cache server");
		return false;
	}
	bool result = execute_file(testcase_path, port);
	stop_server(pid);

	//Cleanup temporary directory
	sprintf(testcase_path, "rm -rF \"%s\"", db);
	system(testcase_path);

	return result;
}