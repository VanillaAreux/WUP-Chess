/* rlink: a small reliable, ordered message link that runs over ANY datagram transport (Wi-Fi UDP today;
 * an infrared transport can plug in later by filling in transport_t). Stop-and-wait with acks, retransmit,
 * keep-alive pings and a checksum on every packet, so it also survives a lossy or noisy channel.
 * Pure C, no libwup dependency: tested on a computer (test_link.c). */
#pragma once
#include <stdint.h>

typedef struct {
    int (*send)(void *ctx, const uint8_t *data, int n);      /* send one datagram to the peer (<0 on error) */
    int (*recv)(void *ctx, uint8_t *data, int max);          /* one datagram from the peer: its length, or 0 if none waiting */
    void *ctx;
} transport_t;

enum { MSG_MOVE = 1, MSG_RESIGN, MSG_DRAW_OFFER, MSG_DRAW_ACCEPT, MSG_DRAW_DECLINE, MSG_REMATCH };
typedef struct { uint8_t kind, a, b, c; } msg_t;             /* MSG_MOVE: a = from, b = to, c = promotion */

#define RLINK_Q          8
#define RLINK_RETRY_MS   250
#define RLINK_PING_MS    1000
#define RLINK_TIMEOUT_MS 6000

typedef struct {
    transport_t t;
    uint32_t (*now)(void);                                   /* milliseconds */
    msg_t sq[RLINK_Q]; int sh, sn; uint16_t head_id; uint32_t last_tx;
    msg_t rq[RLINK_Q]; int rh, rn; uint16_t rx_expected;
    uint32_t last_heard, last_ping; int heard;
} rlink_t;

void rlink_init(rlink_t *l, transport_t t, uint32_t (*now)(void));
int  rlink_send(rlink_t *l, msg_t m);                        /* queue it: 0, or -1 when 8 are already waiting */
int  rlink_recv(rlink_t *l, msg_t *m);                       /* the next received message: 1, or 0 if none */
void rlink_poll(rlink_t *l);                                 /* call every few ms: reads, acks, retransmits, pings */
int  rlink_alive(const rlink_t *l);                          /* the peer was heard within RLINK_TIMEOUT_MS */
int  rlink_pending(const rlink_t *l);                        /* messages sent but not yet acknowledged */
