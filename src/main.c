#include "main.h"

void chroot_into_dir(char * dir) {

	if (chdir(dir) == -1) {
		error("Error changing into new root dir");
	}

	system("cp /usr/local/bin/docker-explorer bin/");

	if (chroot(".") == -1) {
		error("Error chrooting into tmp dir");
	}

}

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);
	char chroot_dir[] = "chroot-cage";

	if (mkdir(chroot_dir, S_IRWXO) == -1) {
		if (errno != EEXIST) {
			// it's ok if file already exists
			error("Error creating a tmp dir");
		}
	}

	char* docker_args[2] = {argv[1], argv[2]};

	if (!strcmp(argv[1], "run")) {
		pull_docker_image(argv[2]);
	
	chroot_into_dir(chroot_dir);

	// We're in parent
	if (argv[3]) {
		Args* args = args_make(argv + 3);
		args->child_pid = clone_child(setup_run_child, (void*) args);
		int exit_code = setup_run_parent(args);
		free(args);
		return exit_code;
	}
	return 0;
}
