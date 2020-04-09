#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);
	 
	int fd_stdout[2];
	int fd_stderr[2];
	char * command = argv[3];

	if (pipe(fd_stdout) == -1) {
		printf("Error creating stdout pipe!");
		return 1;
	}
	if (pipe(fd_stderr) == -1) {
		printf("Error creating stderr pipe!");
		return 1;
	}

	int child_pid = fork();
	
	if (child_pid == -1) {
	     printf("Error forking!");
	     return 1;
	}

	if (child_pid == 0) {
	 	// Replace current program with calling program
		dup2(fd_stdout[1], 1);
		dup2(fd_stderr[1], 2);
		close(fd_stdout[0]);
		close(fd_stderr[0]);
		execvp(command, argv + 3);
	} else {
		// We're in parent
		int status;
		char child_err[1024];
		char child_out[1024];
	 	close(fd_stdout[1]);
		close(fd_stderr[1]);
		waitpid(child_pid, &status, 0);

		size_t out_sz = read(fd_stdout[0], child_out, 1024);
		size_t err_sz = read(fd_stderr[0], child_err, 1024);

		child_out[out_sz] = '\0';
		child_err[err_sz] = '\0';

		fprintf(stdout, "%s", child_out);
		fprintf(stderr, "%s", child_err);
	 }

	return 0;
}
