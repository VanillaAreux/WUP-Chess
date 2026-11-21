/* Two links over a simulated lossy / corrupting channel: every message must arrive once, in order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rlink.h"
typedef struct { uint8_t q[256][16]; int len[256]; int h, n; } chan_t;
typedef struct { chan_t *in, *out; int loss; } ep_t;
static uint32_t clk;
static uint32_t now(void) { return clk; }
static int ep_send(void *c, const uint8_t *d, int n) {
    ep_t *e = c; if (rand() % 100 < e->loss) return n;
    if (e->out->n >= 256) return n; int i = (e->out->h + e->out->n++) % 256; memcpy(e->out->q[i], d, (size_t)n); e->out->len[i] = n;
    if (rand() % 100 < e->loss / 3) e->out->q[i][rand() % n] ^= 0x10;          /* a flipped bit */
    return n;
}
static int ep_recv(void *c, uint8_t *d, int max) {
    ep_t *e = c; if (!e->in->n) return 0; int i = e->in->h; int n = e->in->len[i]; if (n > max) n = max;
    memcpy(d, e->in->q[i], (size_t)n); e->in->h = (e->in->h + 1) % 256; e->in->n--; return n;
}
static int run(int loss) {
    static chan_t ab, ba; memset(&ab, 0, sizeof ab); memset(&ba, 0, sizeof ba); clk = 1000;
    ep_t ea = { &ba, &ab, loss }, eb = { &ab, &ba, loss };
    rlink_t a, b; rlink_init(&a, (transport_t){ ep_send, ep_recv, &ea }, now); rlink_init(&b, (transport_t){ ep_send, ep_recv, &eb }, now);
    int sent_a = 0, sent_b = 0, got_a = 0, got_b = 0, bad = 0; const int N = 300; msg_t m;
    for (int step = 0; step < 200000 && (got_a < N || got_b < N); step++) {
        clk += 5;
        if (sent_a < N && rlink_send(&a, (msg_t){ MSG_MOVE, (uint8_t)(sent_a & 63), (uint8_t)(sent_a >> 6), 7 }) == 0) sent_a++;
        if (sent_b < N && rlink_send(&b, (msg_t){ MSG_MOVE, (uint8_t)(sent_b & 63), (uint8_t)(sent_b >> 6), 9 }) == 0) sent_b++;
        rlink_poll(&a); rlink_poll(&b);
        while (rlink_recv(&b, &m)) { if (m.a != (got_b & 63) || m.b != (got_b >> 6) || m.c != 7) bad++; got_b++; }
        while (rlink_recv(&a, &m)) { if (m.a != (got_a & 63) || m.b != (got_a >> 6) || m.c != 9) bad++; got_a++; }
    }
    printf("loss %2d%%: delivered %d/%d and %d/%d, out of order or duplicated: %d, alive %d%d\n", loss, got_b, N, got_a, N, bad, rlink_alive(&a), rlink_alive(&b));
    return !(got_a == N && got_b == N && !bad);
}
int main(void) {
    int f = 0; f += run(0); f += run(20); f += run(50);
    /* silence: the peer is declared gone */
    static chan_t ab, ba; memset(&ab, 0, sizeof ab); memset(&ba, 0, sizeof ba); clk = 0;
    ep_t ea = { &ba, &ab, 100 }, eb = { &ab, &ba, 0 }; rlink_t a, b;
    rlink_init(&a, (transport_t){ ep_send, ep_recv, &ea }, now); rlink_init(&b, (transport_t){ ep_send, ep_recv, &eb }, now);
    for (int i = 0; i < 400; i++) { clk += 10; rlink_poll(&a); rlink_poll(&b); } int w = rlink_alive(&a);
    clk += 7000; int dead = !rlink_alive(&a); printf("alive after chatter: %d, dead after 7 s silence: %d\n", w, dead); f += !dead;
    puts(f ? "FAILURES" : "ALL OK"); return f;
}
