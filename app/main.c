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

#define STACK_SIZE (1024 * 1024)
#define BUFFER_SIZE 1024

#ifndef CHILD_SIG
#define CHILD_SIG SIGUSR1

#endif

char* DOCKER_IMG = "/usr/local/bin/docker-explorer";

struct ChildArgs {
	int* pipe_args;
	char** exec_args;
};

void error(char* msg) {
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

int setup_run_child(void* setup_args) {
	// TODO: Make variadic
	struct ChildArgs* child_args_ptr = (struct ChildArgs*) setup_args;
	int* pipe_args = child_args_ptr->pipe_args;
	char** exec_args = child_args_ptr->exec_args;
	int fd_outpipe = pipe_args[0];
	int fd_errpipe = pipe_args[1];
	// only executing docker-explorer command for now
	char* command = exec_args[0] = "/docker-explorer";
	
	// open write pipes to talk to parent
	dup2(fd_outpipe, fileno(stdout));
	dup2(fd_errpipe, fileno(stderr));

	if (execv(command, exec_args) == -1) {
		free(setup_args);
		error("Error executing command");
	}

}

int setup_run_parent(int* setup_args) {
	// TODO: Make variadic
	int status;
	char child_err[BUFFER_SIZE] = {'\0'};
	char child_out[BUFFER_SIZE] = {'\0'};

	int fd_outpipe = setup_args[0];
	int fd_errpipe = setup_args[1];

	pid_t child_pid = (pid_t) setup_args[2];

	// non-blocking IO
	fcntl(fd_outpipe, F_SETFL, O_NONBLOCK);
	fcntl(fd_errpipe, F_SETFL, O_NONBLOCK);

	waitpid(child_pid, &status, __WALL | WUNTRACED);

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

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	chroot_into_tmp("tmp-cage/");

	int fd_outpipe[2];
	int fd_errpipe[2];

	if (pipe(fd_outpipe) == -1) {
		error("Error creating stdout pipe");
	}
	if (pipe(fd_errpipe) == -1) {
		error("Error creating the stderr pipe");
	}

	int child_setup_args[] = {fd_outpipe[1], fd_errpipe[1]};
	int parent_setup_args[] = {fd_outpipe[0], fd_errpipe[0], 0};

	struct ChildArgs* child_args = (struct ChildArgs*) malloc(sizeof(struct ChildArgs));

	child_args->pipe_args = child_setup_args;
	child_args->exec_args = argv + 3;

	char* stack = mmap(NULL, STACK_SIZE,
					   PROT_READ | PROT_WRITE,
					   MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
					   -1, 0);

	// stack grows downward
	char* stack_top = stack + STACK_SIZE;

	pid_t child_pid = clone(setup_run_child, stack_top, CLONE_NEWPID | CHILD_SIG, (void*) child_args);

	
	if (child_pid == -1) {
		error("Error cloning");
	}

	// if (!child_pid) {
	// 	// Replace current program with calling program
	// 	// this always exits; success or failure
	// 	setup_run_child(child_setup_args, argv + 3);
	// }

	// We're in parent
	parent_setup_args[2] = (int) child_pid;
	int exit_code = setup_run_parent(parent_setup_args);

	free(child_args);
	return exit_code;
}
