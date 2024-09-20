#!/bin/sh

NAME=aesdchar

do_start() {
  /usr/bin/aesdchar_load
}

do_stop() {
  /usr/bin/aesdchar_unload
}

do_status() {
  lsmod | grep %NAME
}

case "$1" in
  start)
    echo -n "Loading driver: $NAME "
    do_start
    echo "."
    ;;
  stop)
	echo -n "Unloading driver: $NAME "
    do_stop
    echo "."
    ;;
  status)
    do_status
    ;;
  *)
	echo "Usage: $SCRIPTNAME {start|stop|status}" >&2
	exit 3
	;;
esac