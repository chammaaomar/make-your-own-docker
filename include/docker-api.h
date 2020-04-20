#include "requests.h"
#define AUTH_PATH "https://auth.docker.io/token?service=registry.docker.io&scope=repository:library/"
#define DOCKER_BASE "https://registry-1.docker.io/v2/library/"

char* compose_path(char* base, char* img, char* sep, char* ext);

void pull_docker_image(char* img);