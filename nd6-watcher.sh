#!/bin/sh

# PROVIDE: nd6-watcher
# REQUIRE: NETWORKING
# KEYWORD: shutdown

. /etc/rc.subr

name=nd6_watcher
rcvar=`set_rcvar`
command=/usr/local/sbin/nd6-watcher
pidfile=/var/run/nd6-watcher.pid

stop_postcmd=stop_postcmd

stop_postcmd()
{
	rm -f $pidfile
}

nd6_watcher_enable=${nd6_watcher_enable:-"NO"}

load_rc_config $name
run_rc_command "$1"
