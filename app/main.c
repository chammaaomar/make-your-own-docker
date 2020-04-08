#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);

	printf("Your code goes here!\n");

	 
	char *command = argv[3];
	int child_pid = fork();
	if (child_pid == -1) {
	     printf("Error forking!");
	     return 1;
	}	
	if (child_pid == 0) {
	 	   // Replace current program with calling program
	     execv(command, &argv[3]);
	 } else {
	 	   // We're in parent
	 	   int status;
	 	   waitpid(child_pid, &status, 0);
	 	   printf("Child terminated with status %d", WEXITSTATUS(stauts));
	 }

	return 0;
}
