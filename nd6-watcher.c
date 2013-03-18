#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <radlib.h>
#include <libutil.h>
#include "mylog.h"
#include "utils.h"

#define DEFAULT_CONFIG "/usr/local/etc/nd6-watcher.conf"

#define ROUNDUP(a) \
    ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

struct in_addr bind_addr = { INADDR_ANY };
int interrupted=0, rs, debug=0;

SLIST_HEAD(olhead, our_net) our_nets;
struct our_net {
    struct in6_addr addr;
    struct in6_addr mask;

    SLIST_ENTRY(our_net) next;
};

void
signal_handler(int n)
{
    interrupted = n;
}

int
isour_net(struct in6_addr *ip6)
{
    int found = 0;
    struct our_net *net;
    struct in6_addr addr;
    /* Only /64 to compare */

    SLIST_FOREACH(net, &our_nets, next) {
	memcpy(&addr, ip6, sizeof(struct in6_addr));
	/* Skip a router IP */
	if(memcmp(&addr, &net->addr, sizeof(struct in6_addr)) == 0)
	    break;
	APPLY_MASK(&addr, &net->mask);
	if(memcmp(&addr, &net->addr, sizeof(struct in6_addr)) == 0) {
	    found = 1;
	    break;
	}
    }

    return found;
}

void
send_account(struct rad_handle *h, int type, struct in6_addr *addr, struct ether_addr *mac)
{
    if(debug) {
	char buf[100];
	if(type == RAD_START)
	    printf("START ");
	else
	    printf("STOP ");
	printf("%s ", inet_ntop(AF_INET6, addr, buf, sizeof(buf)));
	if(mac)
	    printf("gw %s", ether_ntoa_r(mac, buf));
	printf("\n");
    }

    if(rad_create_request(h, RAD_ACCOUNTING_REQUEST) == -1) {
	mylog(LOG_ERR, "rad_create_request()");
	goto error;
    }
    if(rad_put_int(h, RAD_ACCT_STATUS_TYPE, type) == -1) {
	mylog(LOG_ERR, "rad_put_int(RAD_ACCT_STATUS_TYPE) error");
	goto error;
    }
    if(rad_put_addr(h, RAD_NAS_IP_ADDRESS, bind_addr) == -1) {
	mylog(LOG_ERR, "rad_put_addr(RAD_NAS_IP_ADDRESS) error");
	goto error;
    }
    if(rad_put_int(h, RAD_NAS_PORT, bind_addr.s_addr) == -1) {
	mylog(LOG_ERR, "rad_put_int(RAD_NAS_PORT) error");
	goto error;
    }
    if(mac) {
	if(rad_put_string(h, RAD_USER_NAME, ether_ntoa(mac)) == -1) {
	    mylog(LOG_ERR, "rad_put_string(RAD_USER_NAME) error");
	    goto error;
	}
    } else {
	if(rad_put_string(h, RAD_USER_NAME, "-") == -1) {
	    mylog(LOG_ERR, "rad_put_string(RAD_USER_NAME) error");
	    goto error;
	}
    }
    if(rad_put_attr(h, RAD_LOGIN_IPV6_HOST, addr, sizeof(*addr)) == -1) {
	mylog(LOG_ERR, "rad_put_attr(RAD_LOGIN_IPV6_HOST) error");
	goto error;
    }

    if(rad_send_request(h) == -1)
	mylog(LOG_ERR, "rad_send_request() error");

error:
    return;
}

void
read_rt(struct rad_handle *h, int rs)
{
    int n, len;
    struct rt_msghdr *m;
    struct sockaddr_in6 *dest=NULL;
    struct sockaddr_dl *gw=NULL;
    char *p, buf[2048];

    n = read(rs, buf, sizeof(buf));
    if( n <= 0 ) {
	mylog(LOG_ERR, "no data to read");
	return;
    }
    m = (struct rt_msghdr *)buf;
    if(m->rtm_errno == 0 && (m->rtm_flags & RTF_LLDATA) &&
	    (m->rtm_type == RTM_DELETE || m->rtm_type == RTM_ADD)) {
	p = (char*)(m+1);
	if(m->rtm_addrs & RTA_DST) {
	    dest = malloc(sizeof(struct sockaddr_in6));
	    bzero(dest, sizeof(struct sockaddr_in6));
	    len=SA_SIZE(p);
	    memcpy(dest, p, len);
	    p+=len;
	}
	if(m->rtm_addrs & RTA_GATEWAY) {
	    gw = malloc(sizeof(struct sockaddr_dl));
	    bzero(gw, sizeof(struct sockaddr_dl));
	    len=SA_SIZE(p);
	    memcpy(gw, p, len);
	    p+=len;
	}

	if(dest && gw && gw->sdl_family == AF_LINK && isour_net(&dest->sin6_addr)) {
	    if(m->rtm_type == RTM_DELETE)
		send_account(h, RAD_STOP, &dest->sin6_addr, NULL);
	    else
		send_account(h, RAD_START, &dest->sin6_addr, (struct ether_addr*)&gw->sdl_data);
	}
	if(dest)
	    free(dest);
	if(gw)
	    free(gw);
    }
}

int
make_netmask(struct in6_addr *mask, uint8_t mask_len)
{
    char *p;
    int i, n;

    if(mask_len < 1 || mask_len > 128) {
	mylog(LOG_ERR, "Wrong netmask: %d\n", mask_len);
	return 0;
    }
    p = (char*)mask;

    n = mask_len;
    for(i=0; i<mask_len/8; i++) {
	n-=8;
	*p = 255;
	p++;
    }
    for(i=1;n;i++) {
	n--;
	*p |= 1<<(8-i);
    }
    return 1;
}

int
main(int argc, char *argv[])
{
    int i, n=0, line, our_section=0, net_num=0, servers_num=0;
    fd_set rbits;
    struct pidfh *pfh;
    pid_t opid;
    char c, *config_file=NULL, *dhcprelya_conf=NULL, str[5000], *p, *p1;
    FILE *f;
    struct our_net *net, *net_temp;
    struct rad_handle *h;
    char **servers=NULL, *secret=NULL;
    int timeout=3, tries=3, dead_time=300;
    struct in_addr mask;
    struct ifaddrs *ifaphead, *ifap;
    struct sockaddr_in6 *saddr;

    while ((c = getopt(argc, argv, "df:")) != -1) {
	switch (c) {
	    case 'd':
		debug++;
		break;
	    case 'f':
		config_file = strdup(optarg);
		break;
	    default:
		errx(1, "Unknown option: -%c", c);
	}
    }
    argc -= optind;
    argv += optind;

    SLIST_INIT(&our_nets);

    openmylog("/var/log/nd6-watcher.log", "nd6-watcher", getpid(), 0);
    if(config_file == NULL)
	config_file = strdup(DEFAULT_CONFIG);

    if((f = fopen(config_file, "r")) == NULL)
	errx(1, "Can't open config file: %s", config_file);
    line = 0;
    while (fgets(str, sizeof(str)-1, f) != NULL) {
	line++;
	trim_spaces(str);
	if(strncmp(str, "dhcprelya_conf", 14) == 0) {
	    if((p = strchr(str, '=')) == NULL)
		errx(1, "config file syntax error. line: %d", line);
	    p++;
	    dhcprelya_conf = strdup(p);
	    mylog(LOG_DEBUG, "dhcprelya_conf: %s", dhcprelya_conf);
	} else
	    errx(1, "config file unknown keyword. line: %d", line);
    }
    fclose(f);

    if(dhcprelya_conf == NULL)
	errx(1, "no dhcprelya_conf defined");

    /* Read dhcprelya.conf */
    if((f = fopen(dhcprelya_conf, "r")) == NULL)
	errx(1, "Can't open file: %s", dhcprelya_conf);
    line = 0;

    while (fgets(str, sizeof(str)-1, f) != NULL) {
	line++;
	trim_spaces(str);
	if(str[0] == '#' || str[0] == '\n')
	    continue;
	if(strcmp(str, "[radius-plugin]") == 0) {
	    our_section = 1;
	    continue;
	}
	if(!our_section)
	    continue;
	if(str[0] =='[')
	    break;
	if((p = strchr(str, '=')) == NULL)
	    errx(1, "dhcprelya.conf format error");
	*p = '\0'; p++;
	if(strcasecmp(str, "servers") == 0) {
	    servers_num = 0;
	    for(i=0; i<strlen(p); i++)
		if(p[i] == ' ' || p[i] == '\t')
		    n++;
	    servers = malloc(sizeof(char*)*(n+1));
	    if(servers == NULL)
		errx(1, "malloc()");
	    while((p1 = strsep(&p, " \t")) != NULL) {
		servers[servers_num] = malloc(strlen(p1)+1);
		if(servers[servers_num] == NULL)
		    errx(1, "malloc()");
		strcpy(servers[servers_num], p1);
		servers_num++;
	    }
	} else if(strcasecmp(str, "secret") == 0) {
	    secret = strdup(p);
	} else if(strcasecmp(str, "timeout") == 0) {
	    timeout = strtol(p, NULL, 10);
	    if(timeout < 1)
		errx(1, "timeout error");
	} else if(strcasecmp(str, "tries") == 0) {
	    tries = strtol(p, NULL, 10);
	    if(tries < 1)
		errx(1, "tries error");
	} else if(strcasecmp(str, "dead_time") == 0) {
	    dead_time = strtol(p, NULL, 10);
	    if(dead_time == 0 && errno != 0)
		errx(1, "dead_time error");
	} else if(strcasecmp(str, "bind_to") == 0) {
	    if(inet_pton(AF_INET, p, &bind_addr.s_addr) != 1)
		if(!get_ip(p, &bind_addr.s_addr, &mask.s_addr))
		    errx(1, "bind_to interface %s not found", p);
	} else if(strcasecmp(str, "only_for") == 0) {
	    n=0;
	    for(i=0; i<strlen(p); i++)
		if(p[i] == ' ' || p[i] == '\t')
		    n++;

	    if(getifaddrs(&ifaphead) != 0)
		errx(1, "getifaddrs: %s", strerror(errno));

	    while((p1 = strsep(&p, " \t")) != NULL) {
		char buf[100];
		for(ifap = ifaphead; ifap; ifap = ifap->ifa_next) {
		    if(ifap->ifa_addr->sa_family != AF_INET6)
			continue;
		    saddr = (struct sockaddr_in6*)ifap->ifa_addr;
		    if(IN6_IS_ADDR_LINKLOCAL(&saddr->sin6_addr))
			continue;
		    if(IN6_IS_ADDR_SITELOCAL(&saddr->sin6_addr))
			continue;
		    if(strcmp(ifap->ifa_name, p1) == 0) {
			if((net = malloc(sizeof(struct our_net))) == NULL)
			    errx(1, "malloc()");

			memcpy(&net->addr, &saddr->sin6_addr, sizeof(struct in6_addr));
			saddr = (struct sockaddr_in6*)ifap->ifa_netmask;
			memcpy(&net->mask, &saddr->sin6_addr, sizeof(struct in6_addr));
			mylog(LOG_NOTICE, "%s %s/%s", ifap->ifa_name,
				inet_ntop(AF_INET6, &net->addr, buf, sizeof(buf)-40), 
				inet_ntop(AF_INET6, &net->mask, buf+40, sizeof(buf)-40));
			SLIST_INSERT_HEAD(&our_nets, net, next);
			net_num++;
		    }
		}
	    }
	    freeifaddrs(ifaphead);
	} else
	    errx(1, "dhcprelya.conf format error (unknown option)");
    }
    fclose(f);
    if(net_num == 0)
	errx(1, "No interfaces found. Exiting.");

    if(servers_num < 1)
	errx(1, "No radius servers defined.");

    if(secret == NULL)
	errx(1, "No radius secrets defined.");

    if ((pfh = pidfile_open("/var/run/nd6-watcher.pid", 0644, &opid)) == NULL) {
	if (errno == EEXIST)
	    errx(1, "Already run with PID %d. Exiting.", opid);
	errx(1, "Can't create PID file");
    }

    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    if(!debug && daemon(1,0) == -1)
	errx(1, "Can't daemonize");

    pidfile_write(pfh);
    openmylog("/var/log/nd6-watcher.log", "nd6-watcher", getpid(), 0);

    rs = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
    if(rs < 0)
	errx(1, "Can't create a routing socket");

    mylog(LOG_NOTICE, "Runned.");

    /* Open libradius and add radius servers */
    h = rad_acct_open();
    for(i=0; i < servers_num; i++)
	if(rad_add_server_ex(h, servers[i], 0, secret, timeout, tries, dead_time,
		    &bind_addr) == -1)
	    errx(1, "rad_add_server() error");

    /* MAIN loop */
    FD_ZERO(&rbits);
    FD_SET(rs, &rbits);
    while(!interrupted) {
	n = select(rs+1, &rbits, 0, 0, 0);

	if (n <= 0)
	    continue;
	if (FD_ISSET(rs, &rbits)) {
	    read_rt(h, rs);
	}
    }

    rad_close(h);

    pidfile_remove(pfh);
    SLIST_FOREACH_SAFE(net, &our_nets, next, net_temp) {
	SLIST_REMOVE(&our_nets, net, our_net, next);
	free(net);
    }
    for(i=0; i < servers_num; i++)
	free(servers[i]);
    free(servers);
    free(secret);
    mylog(LOG_NOTICE, "killed with signal %d", interrupted);
    return 0;
}
