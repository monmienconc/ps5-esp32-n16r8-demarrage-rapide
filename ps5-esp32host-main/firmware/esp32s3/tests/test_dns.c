#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dns.h"

int main(void)
{
    uint8_t query[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,
        7,'m','a','n','u','a','l','s',11,'p','l','a','y','s','t','a','t','i','o','n',3,'n','e','t',0,
        0,1,0,1};
    uint8_t reply[512], ip[] = {192,168,4,1};
    size_t n = ps5_dns_reply(query, sizeof(query), reply, sizeof(reply), ip);
    assert(n == sizeof(query) + 16);
    assert(reply[0] == 0x12 && reply[1] == 0x34 && reply[7] == 1);
    assert(!memcmp(reply + n - 4, ip, 4));
    query[sizeof(query)-3] = 28; // AAAA must not be an IPv4 response
    n = ps5_dns_reply(query, sizeof(query), reply, sizeof(reply), ip);
    assert(n == sizeof(query) && reply[7] == 0 && reply[3] == 0);
    query[13] = 'z'; // unknown domain
    n = ps5_dns_reply(query, sizeof(query), reply, sizeof(reply), ip);
    assert(n == sizeof(query) && reply[3] == 3);
    for (size_t i = 0; i < sizeof(query); i++)
        assert(ps5_dns_reply(query, i, reply, sizeof(reply), ip) == 0);
    assert(ps5_dns_reply(query, sizeof(query), reply, 16, ip) == 0);
    query[12] = 0xc0; // reject compressed/cyclic question names
    assert(ps5_dns_reply(query, sizeof(query), reply, sizeof(reply), ip) == 0);
    puts("DNS A, AAAA, NXDOMAIN, truncation and malformed-input checks passed.");
    return 0;
}
