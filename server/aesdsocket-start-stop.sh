#!/bin/bash

DAEMON_DIR="/home/vjp527/git/assignment-1-A-rietz/server"
DAEMON_PATH="$DAEMON_DIR/aesdsocket"
DAEMON_NAME="aesdsocket"

case "$1" in
    start)
    make -C "$DAEMON_DIR" || 
    {
        echo "make in $DAEMON_DIR failed"
        exit 1
    }
    echo "Starting $DAEMON_NAME"
    start-stop-daemon -S -b -x "$DAEMON_PATH" -- -d
    ;;
    stop)
    echo "Stopping $DAEMON_NAME"
    start-stop-daemon -K -x "$DAEMON_PATH"
    ;;
    *)
    echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0