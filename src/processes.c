#include "processes.h"

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

int setup_run_child(void* args) {
	Args* args_struct = (Args*) args;
	char** exec_args = args_struct->exec_opts;
	int fd_outpipe = args_struct->out_pipe[1];
	int fd_errpipe = args_struct->err_pipe[1];
	// only executing docker-explorer command for now
	char* command = exec_args[0];
	
	// open write pipes to talk to parent
	dup2(fd_outpipe, fileno(stdout));
	dup2(fd_errpipe, fileno(stderr));

	if (execvp(command, exec_args) == -1) {
		free(args);
		error("Error executing command");
	}

}