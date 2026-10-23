#include "dns_core.h"

#include <string.h>

#define HDR 12

int dc_answer(const uint8_t *q, size_t qlen, uint8_t *out, size_t cap, const uint8_t ip[4])
{
    if (qlen < HDR + 5 || qlen > cap - 16 || (q[2] & 0x80) || q[4] != 0 || q[5] != 1) {
        return 0;  // a reply, or not exactly one question
    }
    size_t p = HDR;
    while (p < qlen && q[p] != 0) {
        if (q[p] & 0xC0) {
            return 0;  // no compression in a question we answer
        }
        p += (size_t)q[p] + 1;
    }
    if (p + 5 > qlen) {
        return 0;
    }
    size_t qend = p + 5;  // terminator, QTYPE, QCLASS
    int is_a = q[p + 1] == 0 && q[p + 2] == 1 && q[p + 3] == 0 && q[p + 4] == 1;

    memcpy(out, q, qend);
    out[2] = 0x84 | (q[2] & 0x01);  // QR, AA, keep RD
    out[3] = 0x00;                  // RA = 0, RCODE = NOERROR
    out[6] = 0;
    out[7] = is_a ? 1 : 0;           // ANCOUNT
    out[8] = out[9] = out[10] = out[11] = 0;
    size_t n = qend;
    if (is_a) {
        static const uint8_t rr[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
        memcpy(out + n, rr, sizeof rr);
        n += sizeof rr;
        memcpy(out + n, ip, 4);
        n += 4;
    }
    return (int)n;
}
