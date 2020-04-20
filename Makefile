vpath %.c src utils
vpath %.h include
vpath %.o obj

error.o: error.c error.h
	gcc -I include -c utils/error.c -o obj/error.o
processes.o: processes.c processes.h error.c
	gcc -I include -c src/processes.c -o obj/processes.o
requests.o: requests.c requests.h
	gcc -I include -c src/requests.c -o obj/requests.o
docker-api.o: docker-api.c docker-api.h
	gcc -I include -c src/docker-api.c -o obj/docker-api.o
main.o: main.c main.h processes.c docker-api.c error.c
	gcc -I include -c src/main.c -o obj/main.o

install: processes.o requests.o docker-api.o main.o error.o
	gcc -lcurl obj/main.o obj/docker-api.o obj/requests.o obj/processes.o obj/error.o -o docker-app
