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

// pid_t clone_child(int fn (void*), void* args) {
// 	char* stack = mmap(NULL, STACK_SIZE,
// 					   PROT_READ | PROT_WRITE,
// 					   MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
// 					   -1, 0);

// 	// stack grows downward
// 	char* stack_top = stack + STACK_SIZE;

// 	int child_pid = clone(setup_run_child, stack_top, CLONE_NEWPID, (void*) args);

// 	if (child_pid == -1) {
// 		free(args);
// 		error("Error cloning child process");
// 	}

// 	return child_pid;
// }

size_t write_callback(void* data, size_t size, size_t nmemb, void* array_mem) {
	// libcurl always calls the callback with size == 1;
	size_t true_sz = size*nmemb;
	BufferArray* buf_arr = (BufferArray*) array_mem;
	memcpy(buf_arr->buffer + buf_arr->size, data, true_sz);
	buf_arr->size += true_sz;
	return true_sz;
}

char* extract_string_from_json(char** buffer_ptr, char* field) {
	/*
	Side Effect: Moves the buffer pointed to by @buffer_ptr to the end of
	the target. Thus the pointer referenced by @buffer_ptr acts like a
	cursor that can be iteratively used to search a JSON response for more
	fields until exhausted.
	*/
	// e.g. "token":" thus strlen(token) + 4 chars + null terminator
	char* buffer = *buffer_ptr;
	char* start_str = (char*) calloc(strlen(field) + 5, sizeof(char));
	start_str[0] = '"';
	strcat(start_str, field);
	strcat(start_str, "\":\"");
	char* target_start = strstr(buffer, start_str);
	if (!target_start) {
		fprintf(stderr, "Could not find field: %s in JSON response", field);
		return NULL;
	}
	// the JSON response has "field":"$FIELD", we skip the keywords and formatting
	// so we move forward by len("field":")
	target_start += strlen(start_str);
	char* target_end = strchr(target_start, '"');
	if (!target_end) {
		fprintf(stderr, "Error: end of string corresponding to %s field not found; "
						"make sure JSON value at target field is actually a string", field);
		return NULL;
	}
	// the access token ends with $TOKEN" so the pointer is pointing to -> " at the end
	// so we have to displace it one step backward.
	target_end += -1;
	size_t target_len = (size_t) (target_end - target_start);
	if (target_len > 0) {
		char* target = (char*) calloc(target_len + 2, sizeof(char));
		memcpy(target, target_start, target_len + 1);
		// displace the buffer to the end of the target, so it can be used
		// to search iteratively for searching next instance of the field
		*buffer_ptr = target_end + 1;
		return target;
	}
	
	fprintf(stderr, "Error: target string (JSON response) had length: %i", (int) target_len);
	return NULL;

}

char* make_curl_req(char* url, size_t write_cb (void*, size_t, size_t, void*), struct curl_slist* list) {
	CURL* curl = curl_easy_init();
	char* body_buffer = (char*) calloc(CURL_MAX_WRITE_SIZE, sizeof(char));
	BufferArray buf_arr;
	buf_arr.buffer = body_buffer;
	buf_arr.size = 0;
	if (curl) {
		CURLcode res;
		if (list) {
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
		}
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

char* compose_string(char* base, char* img, char* ext) {
	/*
	Builds the url for a request to the docker API v2. If making a request to the auth server,
	the ext should be an action preceded by ':', like ':pull' or ':push'. If making a request
	to the registry, the ext should be a resource like /manifests/latest.
	
	params:
		@base: base url, including protocol.
		@img: assumed to be Official Docker image, so it falls under the library repo
		@ext: e.g. ":pull" or "/tags/list". Must be preceded by ':' or '/'
	*/
	char* url = (char*) calloc(strlen(base) + strlen(img) + strlen(ext) + 1, sizeof(char));
	strcat(url, base);
	strcat(url, img);
	strcat(url, ext);

	return url;
}

void pull_docker_image(char* img) {
	char* auth_url = compose_string(
						"https://auth.docker.io/token?service=registry.docker.io&scope=repository:library/",
						img,
						":pull");
	char* auth_response = make_curl_req(auth_url, write_callback, NULL);

	char* token = extract_string_from_json(&auth_response,"token");

	if (!token) {
		// failed to get auth token; extract_token already writes to stderr
		// so just exit
		free(token);
		exit(1);
	}
	char* manifests_url = compose_string("https://registry-1.docker.io/v2/library/", img, "/manifests/latest");
	struct curl_slist* headers = NULL;
	// Authorization: Bearer $TOKEN
	char* auth_header = (char*) calloc(strlen(token) + 50, sizeof(char));
	strcpy(auth_header, "Authorization: Bearer ");
	strcat(auth_header, token);
	headers = curl_slist_append(headers, auth_header);

	char* manifests_response = make_curl_req(manifests_url, write_callback, headers);
	
	curl_slist_free_all(headers);
	puts("Auth response:");
	puts(auth_response);
	
	puts("Manifests reponse:");
	puts(manifests_response);
	free(manifests_url);
	free(auth_url);
	free(auth_header);
	free(token);
	

}

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	// chroot_into_tmp("tmp-cage/");

	char* docker_args[2] = {argv[1], argv[2]};

	if (!strcmp(argv[1], "run")) {
		pull_docker_image(argv[2]);
	}

	// // We're in parent
	// Args* args = args_make(argv + 3);

	// args->child_pid = clone_child(setup_run_child, (void*) args);
	// int exit_code = setup_run_parent(args);

	// free(args);
	// return exit_code;
}
