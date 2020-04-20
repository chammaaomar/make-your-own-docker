#include "base.h"
#include <sys/types.h>
#define _GNU_SOURCE
#include <sched.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024
#define STACK_SIZE (1024 * 1024)

typedef struct Args {
	int* err_pipe;
	int* out_pipe;
	char** exec_opts;
	pid_t child_pid;
} Args;

Args* args_make(char* cmd_args[]);

pid_t clone_child(int fn (void*), void* args);

int setup_run_parent(void* args);

int setup_run_child(void* args);