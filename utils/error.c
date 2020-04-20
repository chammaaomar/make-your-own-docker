#include "error.h"

void error(char* msg) {
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(1);
}