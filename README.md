# CDocker

Head over to [codecrafters.io](https://codecrafters.io) to signup for early access. Codecrafters is a pretty cool challenge based on gamifying the idea of make-your-own-X. Your entries to the challenge are verified for correctness by submitting to a pretty cool Contiuous Integration server. You get nicely colored visuals and instant feedback. Plus, there's a pretty cool Discord community you can join! It's a pretty nice application of CI. New challenges are always being added! 

## Usage
To get started you will need a docker daemon. You can get Docker desktop for macOS or Windows from [here](https://www.docker.com/products/docker-desktop).

From the root directory in this repo, run the following
```bash
docker build --tag docker-clone .
docker run --cap-add SYS_ADMIN docker-clone run alpine ash
```

We are running in a docker container because commands like cloning a child process are linux-specific, so they cannot run on macOS or Windows. Also:

* SYS_ADMIN: to be able to chroot into the docker container.
* run alpine: You can generally write `run image` for any *official* docker image. See [here](https://hub.docker.com/search?q=&type=image&image_filter=official) for a list of official images. There are many like nginx, redis, mongoDB etc.
* ash: this would be whatever binary you want to launch upon entry. If you want to interactively use the terminal in the container, use

```bash
docker run --interactive --tty --cap-add SYS_ADMIN docker clone run alpine ash
```

## Limitations
The main limitations are

* pulling images is currently restricted to the official images
* filesystem isolation is implemented via chroot, and process namespace isolation is also implemented (the entrypoint binary sees itself as PID number 1), but compute, memory, network etc isn't isolated; cgroups aren't implemented, so for a heavy workload the container can use 100\% CPU of the host machine.
