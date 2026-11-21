#include "rlink.h"
#include <string.h>

enum { P_DATA = 1, P_ACK = 2, P_PING = 3 };

static uint8_t chk(const uint8_t *d, int n) { uint8_t c = 0x5a; for (int i = 0; i < n; i++) c = (uint8_t)((c << 1 | c >> 7) ^ d[i]); return c; }
static int tx(rlink_t *l, uint8_t *d, int n) { d[n] = chk(d, n); return l->t.send(l->t.ctx, d, n + 1); }

static void send_head(rlink_t *l) {
    uint8_t d[10]; msg_t m = l->sq[l->sh];
    d[0] = 'W'; d[1] = 'C'; d[2] = P_DATA; d[3] = (uint8_t)l->head_id; d[4] = (uint8_t)(l->head_id >> 8);
    d[5] = m.kind; d[6] = m.a; d[7] = m.b; d[8] = m.c;
    tx(l, d, 9); l->last_tx = l->now();
}
static void send_ack(rlink_t *l, uint16_t id) {
    uint8_t d[6] = { 'W', 'C', P_ACK, (uint8_t)id, (uint8_t)(id >> 8), 0 }; tx(l, d, 5);
}

void rlink_init(rlink_t *l, transport_t t, uint32_t (*now)(void)) {
    memset(l, 0, sizeof *l); l->t = t; l->now = now; l->head_id = 1; l->rx_expected = 1;
    l->last_heard = l->last_ping = l->last_tx = now();
}

int rlink_send(rlink_t *l, msg_t m) {
    if (l->sn >= RLINK_Q) return -1;
    l->sq[(l->sh + l->sn) % RLINK_Q] = m;
    if (l->sn++ == 0) send_head(l);
    return 0;
}

int rlink_recv(rlink_t *l, msg_t *m) {
    if (!l->rn) return 0;
    *m = l->rq[l->rh]; l->rh = (l->rh + 1) % RLINK_Q; l->rn--; return 1;
}

int rlink_pending(const rlink_t *l) { return l->sn; }
int rlink_alive(const rlink_t *l) { return l->heard && (uint32_t)(l->now() - l->last_heard) < RLINK_TIMEOUT_MS; }

void rlink_poll(rlink_t *l) {
    uint8_t d[32]; int n, guard = 32;
    while (guard-- > 0 && (n = l->t.recv(l->t.ctx, d, sizeof d)) > 0) {
        if (n < 4 || d[0] != 'W' || d[1] != 'C' || chk(d, n - 1) != d[n - 1]) continue;       /* damaged or not ours */
        l->last_heard = l->now(); l->heard = 1;
        if (d[2] == P_DATA && n == 10) {
            uint16_t id = (uint16_t)(d[3] | d[4] << 8);
            if (id == l->rx_expected) {
                if (l->rn >= RLINK_Q) continue;                                                  /* no room: unacked, the peer retries */
                l->rq[(l->rh + l->rn++) % RLINK_Q] = (msg_t){ d[5], d[6], d[7], d[8] };
                l->rx_expected++; send_ack(l, id);
            } else if ((uint16_t)(l->rx_expected - id) < 1000) send_ack(l, id);                  /* a repeat: our ack was lost */
        } else if (d[2] == P_ACK && n == 6) {
            uint16_t id = (uint16_t)(d[3] | d[4] << 8);
            if (l->sn && id == l->head_id) {
                l->sh = (l->sh + 1) % RLINK_Q; l->sn--; l->head_id++;
                if (l->sn) send_head(l);
            }
        }
    }
    uint32_t now = l->now();
    if (l->sn && (uint32_t)(now - l->last_tx) >= RLINK_RETRY_MS) send_head(l);
    if ((uint32_t)(now - l->last_ping) >= RLINK_PING_MS && (uint32_t)(now - l->last_tx) >= RLINK_PING_MS) {
        uint8_t p[5] = { 'W', 'C', P_PING, 0, 0 }; tx(l, p, 3); l->last_ping = now;
    }
}
