#!/bin/sh
set -e
make install
exec /app/docker-app "$@"
