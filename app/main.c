#include <stdio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

char* DOCKER_IMG = "/usr/local/bin/docker-explorer";

void error(char* msg) {
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

int setup_run_child(int* setup_args, char* exec_args[]) {
	// TODO: Make variadic
	int fd_outpipe = setup_args[0];
	int fd_errpipe = setup_args[1];
	// only executing docker-explorer command for now
	char* command = exec_args[0] = "/docker-explorer";
	
	// open write pipes to talk to parent
	dup2(fd_outpipe, fileno(stdout));
	dup2(fd_errpipe, fileno(stderr));

	if (execv(command, exec_args)) {
		error("Error executing command");
	}
}

void setup_run_parent(int* setup_args) {
	// TODO: Make variadic
	int status;
	char child_err[1024];
	char child_out[1024];

	int fd_outpipe = setup_args[0];
	int fd_errpipe = setup_args[1];

	pid_t child_pid = (pid_t) setup_args[2];

	// non-blocking IO
	fcntl(fd_outpipe, F_SETFL, O_NONBLOCK);
	fcntl(fd_errpipe, F_SETFL, O_NONBLOCK);

	waitpid(child_pid, &status, 0);

	size_t out_sz = read(fd_outpipe, child_out, 1024);
	size_t err_sz = read(fd_errpipe, child_err, 1024);

	child_out[out_sz] = '\0';
	child_err[err_sz] = '\0';

	fprintf(stdout, "%s", child_out);
	fprintf(stderr, "%s", child_err);

	exit(WEXITSTATUS(status));
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

	pid_t child_pid = fork();

	
	if (child_pid == -1) {
		error("Error forking");
	}

	if (!child_pid) {
		// Replace current program with calling program
		// this always exits; success or failure
		setup_run_child(child_setup_args, argv + 3);
	}

	// We're in parent
	parent_setup_args[2] = (int) child_pid;
	setup_run_parent(parent_setup_args);
	
}
