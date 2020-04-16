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

typedef struct BufferArray {
	char* buffer;
	int size;
} BufferArray;

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

	// waitpid(child_pid, &status, __WALL);

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

size_t write_callback(void* data, size_t size, size_t nmemb, void* array_mem) {
	// libcurl always calls the callback with size == 1;
	size_t true_sz = size*nmemb;
	BufferArray* buf_arr = (BufferArray*) array_mem;
	memcpy(buf_arr->buffer + buf_arr->size, data, true_sz);
	buf_arr->size += true_sz;
	return true_sz;
}

char* extract_token(char* buffer, char* token_start_kw, char* token_end_kw) {
	// returns NULL to signal failure
	char* token_start = strstr(buffer, token_start_kw);
	char* token_end = strstr(buffer, token_end_kw) ;
	if (!token_start || !token_end) {
		fprintf(stderr, "Error token invalid");
		return NULL;
	}
	// the JSON response has "token": "$TOKEN", we skip the keywords and formatting
	token_start += strlen(token_start_kw)+1;
	// the access token ends with $TOKEN","access_token", so we move three steps
	// back to get the end
	token_end += -3;
	size_t token_len = (size_t) (token_end - token_start);
	if (token_len > 0) {
		char* token = (char*) calloc(token_len + 2, sizeof(char));
		memcpy(token, token_start, token_len + 1);
		return token;
	} else {
		fprintf(stderr, "Error token invalid");
		return NULL;
	}

}

char* make_curl_req(char* url, size_t write_cb (void*, size_t, size_t, void*)) {
	CURL* curl = curl_easy_init();
	char* body_buffer = (char*) calloc(CURL_MAX_WRITE_SIZE, sizeof(char));
	BufferArray buf_arr;
	buf_arr.buffer = body_buffer;
	buf_arr.size = 0;
	if (curl) {
		CURLcode res;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*) &buf_arr);
		res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}
		curl_easy_cleanup(curl);
	}
	return body_buffer;
}

void pull_docker_image(char* img) {
	char* auth_url = "https://auth.docker.io/token?service=registry.docker.io&scope=repository:library/ubuntu:pull";
	char* auth_response = make_curl_req(auth_url, write_callback);

	char* token = extract_token(auth_response,"\"token\":", "\"access_token\":");

	if (!token) {
		exit(1);
	}

	puts(token);

	free(token);
	

}

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	chroot_into_tmp("tmp-cage/");

	char* docker_args[2] = {argv[1], argv[2]};

	if (!strcmp(argv[1], "run")) {
		pull_docker_image(argv[2]);
	}

	pull_docker_image("");

	// We're in parent
	Args* args = args_make(argv + 3);

	args->child_pid = clone_child(setup_run_child, (void*) args);
	int exit_code = setup_run_parent(args);

	free(args);
	return exit_code;
}
