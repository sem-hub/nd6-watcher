#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include <pthread.h>
#include "mylog.h"
#include "utils.h"

extern int debug;
static char logfilename[MAX_NAME_SIZE];
static char progname[MAX_NAME_SIZE];
static pid_t log_pid;
static int log_initialized=0, add_tid;

int
openmylog(char *filename, char *name, pid_t pid, int need_tid)
{
    /* XXX check stings sizes */
    strlcpy(logfilename, filename, sizeof(logfilename));
    strlcpy(progname, name, sizeof(progname));
    log_pid = pid;
    log_initialized=1;
    add_tid = need_tid;
    return 1;
}

void
mylog(int priority, const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    FILE *f;
    char now[100], str[160];
    struct tm tm;
    struct timeval tv;

    /* XXX fatal? */
    if(!log_initialized)
	return;

    if(debug) {
	vprintf(message, ap);
	printf("\n");
    } else
	if(priority != LOG_DEBUG) {
	    if((f = fopen(logfilename, "a")) != NULL) {
		gettimeofday(&tv, NULL);
		localtime_r(&tv.tv_sec, &tm);
		strftime(now, sizeof(now), "%D %T.", &tm);
		if(add_tid) {
		    snprintf(str, sizeof(str), "%s%.6u %s[%u:%lu]: ", now,
			    (int)tv.tv_usec, progname, log_pid,
			    (long)pthread_self());
		} else
		    snprintf(str, sizeof(str), "%s%.6u %s[%u]: ", now,
			    (int)tv.tv_usec, progname, log_pid);
		strlcat(str, message, sizeof(str));
		strlcat(str, "\n", sizeof(str));
		vfprintf(f, str, ap);
		fclose(f);
	    }
	}
}

