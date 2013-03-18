#ifndef __UTILS_H
#define __UTILS_H

/* Apply ipv6 mask on ipv6 addr */
#define APPLY_MASK(addr,mask)                     \
    (addr)->s6_addr32[0] &= (mask)->s6_addr32[0]; \
    (addr)->s6_addr32[1] &= (mask)->s6_addr32[1]; \
    (addr)->s6_addr32[2] &= (mask)->s6_addr32[2]; \
    (addr)->s6_addr32[3] &= (mask)->s6_addr32[3];
#ifdef __FreeBSD__
#define s6_addr32 __u6_addr.__u6_addr32
#endif

void trim_spaces(char *);
int get_ip(char *, in_addr_t *, in_addr_t *);

#endif
