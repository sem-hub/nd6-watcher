#include <unistd.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/ioctl.h>
#include <sys/mac.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include "mylog.h"

void
trim_spaces(char *str)
{
    int i=0, j=0;

    while(str[j] == '0' && str[j] != '\0')
	j++;

    while(str[j] != '\0') {
	if((str[j] == ' ' || str[j] == '\t') &&
	   (str[j+1] == ' ' || str[j+1] == '\t' || str[j+1] == '\n')) {
	    j++;
	    continue;
	}
	if(str[j] == '\n')
	    j++;
	str[i] = str[j];
	i++; j++;
    }
    str[j] = '\0';
}


int 
get_ip(char *iname, in_addr_t *ip, in_addr_t *mask)
{
    int s;
    struct ifreq ifr;
    struct sockaddr_in *saddr;

    if((s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	errx(1, "get_ip: socket: %s", strerror(errno));

    strlcpy(ifr.ifr_name, iname, sizeof(ifr.ifr_name));
    if(ioctl(s, SIOCGIFADDR, &ifr) < 0) {
	close(s);
	mylog(LOG_DEBUG, "Can't get IP for %s", iname);
	return 0;
    }

    saddr = (struct sockaddr_in*)&ifr.ifr_addr;
    memcpy(ip, &saddr->sin_addr.s_addr, sizeof(in_addr_t));

    if(ioctl(s, SIOCGIFNETMASK, &ifr) < 0) {
	close(s);
	mylog(LOG_DEBUG, "Can't get netmask for %s", iname);
	return 0;
    }

    memcpy(mask, &saddr->sin_addr.s_addr, sizeof(in_addr_t));

    close(s);
    return 1;
}
