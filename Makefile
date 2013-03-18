all: utils.o mylog.o nd6-watcher

CFLAGS+=	-g

nd6-watcher.o: nd6-watcher.c utils.h mylog.h
	gcc ${CFLAGS} -c nd6-watcher.c -Wall

utils.o: utils.c utils.h mylog.h
	gcc ${CFLAGS} -c utils.c -Wall

mylog.o: mylog.c mylog.h
	gcc ${CFLAGS} -c mylog.c -Wall

nd6-watcher: nd6-watcher.o
	gcc nd6-watcher.o utils.o mylog.o -lutil -lradius -o nd6-watcher

install:
	install -s -m 0555 nd6-watcher /usr/local/sbin
	install -m 0555 nd6-watcher.sh /usr/local/etc/rc.d/nd6-watcher

clean:
	rm -f nd6-watcher *.o *.core
