#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON_PATH="$SCRIPT_DIR/aesdsocket"
DAEMON_NAME="aesdsocket"

case "$1" in
    start)
    echo "Starting $DAEMON_NAME"
    mkdir -p /var/tmp
    chmod 777 /var/tmp
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