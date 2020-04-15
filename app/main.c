#include <stdio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#define _GNU_SOURCE
#include <sched.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <curl/curl.h>

#define STACK_SIZE (1024 * 1024)
#define BUFFER_SIZE 1024

typedef struct Args {
	int* err_pipe;
	int* out_pipe;
	char** exec_opts;
	pid_t child_pid;
} Args;

char* DOCKER_IMG = "/usr/local/bin/docker-explorer";

void error(char* msg) {
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

int setup_run_child(void* args) {
	Args* args_struct = (Args*) args;
	char** exec_args = args_struct->exec_opts;
	int fd_outpipe = args_struct->out_pipe[1];
	int fd_errpipe = args_struct->err_pipe[1];
	// only executing docker-explorer command for now
	char* command = exec_args[0] = "/docker-explorer";
	
	// open write pipes to talk to parent
	dup2(fd_outpipe, fileno(stdout));
	dup2(fd_errpipe, fileno(stderr));

	if (execv(command, exec_args) == -1) {
		free(args);
		error("Error executing command");
	}

}

int setup_run_parent(void* args) {
	Args* args_struct = (Args*) args;
	int status;
	char child_err[BUFFER_SIZE] = {'\0'};
	char child_out[BUFFER_SIZE] = {'\0'};

	int fd_outpipe = args_struct->out_pipe[0];
	int fd_errpipe = args_struct->err_pipe[0];

	pid_t child_pid = args_struct->child_pid;

	// non-blocking IO
	fcntl(fd_outpipe, F_SETFL, O_NONBLOCK);
	fcntl(fd_errpipe, F_SETFL, O_NONBLOCK);

	waitpid(child_pid, &status, __WALL);

	size_t out_sz = read(fd_outpipe, child_out, BUFFER_SIZE - 1);
	size_t err_sz = read(fd_errpipe, child_err, BUFFER_SIZE - 1);

	fprintf(stdout, "%s", child_out);
	fprintf(stderr, "%s", child_err);

	return WEXITSTATUS(status);
}

void chroot_into_tmp(const char * tmp_dir) {
	
	if (mkdir(tmp_dir, S_IRWXO) == -1) {
		if (errno != EEXIST) {
			// it's ok if file already exists
			error("Error creating a tmp dir");
		}
	}

	pid_t cp_pid = fork();

	if (cp_pid == -1) {
		error("Error forking to copy executable");
	}

	if (!cp_pid) {
		// copy process; this will exit upon success or failure
		if (execlp("cp", "cp", DOCKER_IMG, tmp_dir, NULL) == -1) {
			error("Error copying executable into chroot cage");
		}
	}

	int status;
	waitpid(cp_pid, &status, 0);

	if (chroot(tmp_dir) == -1) {
		error("Error chrooting into tmp dir");
	}
}

Args* args_make(char* cmd_args[]) {

	int fd_outpipe[2];
	int fd_errpipe[2];

	if (pipe(fd_outpipe) == -1) {
		error("Error creating stdout pipe");
	}
	if (pipe(fd_errpipe) == -1) {
		error("Error creating the stderr pipe");
	}

	Args* args = (Args*) malloc(sizeof(Args));
	args->err_pipe = fd_errpipe;
	args->out_pipe = fd_outpipe;
	args->exec_opts = cmd_args;

	return args;
}

pid_t clone_child(int fn (void*), void* args) {
	char* stack = mmap(NULL, STACK_SIZE,
					   PROT_READ | PROT_WRITE,
					   MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
					   -1, 0);

	// stack grows downward
	char* stack_top = stack + STACK_SIZE;

	int child_pid = clone(setup_run_child, stack_top, CLONE_NEWPID, (void*) args);

	if (child_pid == -1) {
		free(args);
		error("Error cloning child process");
	}

	return child_pid;
}

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	chroot_into_tmp("tmp-cage/");

	// We're in parent
	Args* args = args_make(argv + 3);

	args->child_pid = clone_child(setup_run_child, (void*) args);
	int exit_code = setup_run_parent(args);

	free(args);
	return exit_code;
}
