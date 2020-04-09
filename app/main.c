#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

void error(char* msg) {
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);
	 
	int fd_outpipe[2];
	int fd_errpipe[2];
	char * command = argv[3];

	if (pipe(fd_outpipe) == -1) {
		error("Error creating stdout pipe");
	}
	if (pipe(fd_errpipe) == -1) {
		error("Error creating the stderr pipe");
	}

	int child_pid = fork();
	
	if (child_pid == -1) {
		error("Error forking");
	}

	if (child_pid == 0) {
	 	// Replace current program with calling program
		dup2(fd_outpipe[1], 1);
		dup2(fd_errpipe[1], 2);
		close(fd_outpipe[0]);
		close(fd_errpipe[0]);
		execvp(command, argv + 3);
	} else {
		// We're in parent
		int status;
		char child_err[1024];
		char child_out[1024];
	 	
		close(fd_outpipe[1]);
		close(fd_errpipe[1]);

		size_t out_sz = read(fd_outpipe[0], child_out, 1024);
		size_t err_sz = read(fd_errpipe[0], child_err, 1024);

		waitpid(child_pid, &status, 0);

		child_out[out_sz] = '\0';
		child_err[err_sz] = '\0';

		fprintf(stdout, "%s", child_out);
		fprintf(stderr, "%s", child_err);

		return WEXITSTATUS(status);
	 }

	return 0;
}
