#include "dns.h"
#include <string.h>

static uint16_t read16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }

size_t ps5_dns_reply(const uint8_t *q, size_t size, uint8_t *out,
                     size_t capacity, const uint8_t ip[4])
{
    if (size < 17 || read16(q + 4) != 1 || (q[2] & 0xf8) != 0) return 0;

    // On saute simplement le nom demandé, sans le comparer.
    size_t pos = 12;
    while (pos < size && q[pos]) {
        unsigned label = q[pos++];
        // Pas de compression dans la question (pas de pointeurs non fiables).
        if (label > 63 || pos + label >= size) return 0;
        pos += label;
    }
    if (pos + 5 > size) return 0;
    pos++; // octet nul final

    uint16_t type = read16(q + pos), klass = read16(q + pos + 2);
    pos += 4;
    if (klass != 1 || capacity < pos + 16) return 0;

    memcpy(out, q, pos);
    out[2] = 0x84 | (q[2] & 1);   // réponse, autoritaire, copie RD
    out[3] = 0;                   // toujours NOERROR
    memset(out + 6, 0, 6);        // compteurs réponse/autorité/additionnel

    if (type != 1) return pos;    // AAAA/HTTPS/etc. : NOERROR sans donnée (évite les timeouts)

    out[7] = 1;
    const uint8_t record[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
    memcpy(out + pos, record, sizeof(record));
    memcpy(out + pos + sizeof(record), ip, 4);
    return pos + 16;
}
