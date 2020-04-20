#include "chroot.h"

void chroot_into_tmp(char * chroot_dir) {

	if (chdir(chroot_dir) == -1) {
		error("Error changing into new root dir");
	}

	system("cp /usr/local/bin/docker-explorer bin/");

	if (chroot(".") == -1) {
		error("Error chrooting into tmp dir");
	}

}