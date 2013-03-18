#ifndef __MYLOG_H
#define __MYLOG_H
#include <sys/types.h>
#include <syslog.h>

#define MAX_NAME_SIZE 64

int openmylog(char *filename, char *progname, pid_t pid, int need_tid);
void mylog(int priority, const char *message, ...);

#endif
