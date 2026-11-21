#include "wifi.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#ifndef WIFI_HOST_TEST
#include "wup.h"
#endif

enum { H_ANN = 1, H_JOIN = 2, H_JOINOK = 3 };

static void send_to(wifi_t *w, uint32_t ip, const uint8_t *d, int n) {
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(WIFI_PORT); a.sin_addr.s_addr = ip;
    sendto(w->fd, d, (size_t)n, 0, (struct sockaddr *)&a, sizeof a);
}
static void hello(wifi_t *w, uint32_t ip, int type) {
    uint8_t d[19] = { 'W', 'H', (uint8_t)type }; memcpy(d + 3, w->name, 16); send_to(w, ip, d, 19);
}

const char *wifi_ip_str(void) {
#ifndef WIFI_HOST_TEST
    return wup_net_ip_str();
#else
    return "127.0.0.1";
#endif
}

int wifi_open(wifi_t *w, uint32_t (*now)(void)) {
    memset(w, 0, sizeof *w); w->fd = -1; w->now = now;
#ifndef WIFI_HOST_TEST
    if (wup_net_init() < 0) return -1;
#endif
    w->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (w->fd < 0) return -2;
    int one = 1; setsockopt(w->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); setsockopt(w->fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(WIFI_PORT); a.sin_addr.s_addr = htonl(0);
    if (bind(w->fd, (struct sockaddr *)&a, sizeof a) < 0) { wifi_close(w); return -3; }
    fcntl(w->fd, F_SETFL, fcntl(w->fd, F_GETFL, 0) | O_NONBLOCK);
    const char *s = wifi_ip_str(); const char *dot = strrchr(s, '.');
    snprintf(w->name, sizeof w->name, "Pad %s", dot ? dot + 1 : "?");
    return 0;
}

void wifi_close(wifi_t *w) { if (w->fd >= 0) { close(w->fd); w->fd = -1; } w->mode = WIFI_IDLE; w->joined = 0; w->peer_ip = 0; }
void wifi_host(wifi_t *w) { w->mode = WIFI_HOSTING; w->joined = 0; w->peer_ip = 0; w->last_ann = w->now() - 10000; }
void wifi_scan(wifi_t *w) { w->mode = WIFI_SCANNING; w->joined = 0; w->peer_ip = 0; memset(w->hosts, 0, sizeof w->hosts); }
void wifi_join(wifi_t *w, uint32_t ip) { w->mode = WIFI_JOINING; w->joined = 0; w->peer_ip = ip; w->last_ann = w->now() - 10000; }
int  wifi_connected(const wifi_t *w) { return w->joined; }

int wifi_hosts(wifi_t *w, wifi_host_t *out, int max) {
    int n = 0; uint32_t now = w->now();
    for (int i = 0; i < WIFI_MAX_HOSTS && n < max; i++) if (w->hosts[i].ip && (uint32_t)(now - w->hosts[i].seen) < 3000) out[n++] = w->hosts[i];
    return n;
}

/* read datagrams: lobby packets are handled here; the first game packet from the peer is returned in buf */
static int pump(wifi_t *w, uint8_t *buf, int max) {
    uint8_t d[64];
    for (int guard = 0; guard < 16; guard++) {
        struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = (int)recvfrom(w->fd, d, sizeof d, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) return 0;
        uint32_t ip = from.sin_addr.s_addr;
        if (n >= 2 && d[0] == 'W' && d[1] == 'C') {
            if (w->peer_ip && ip == w->peer_ip && w->mode != WIFI_IDLE) {
                w->joined = 1;
                if (n > max) n = max;
                memcpy(buf, d, (size_t)n); return n;
            }
        } else if (n >= 19 && d[0] == 'W' && d[1] == 'H') {
            if (d[2] == H_ANN && w->mode == WIFI_SCANNING) {
                int slot = -1;
                for (int i = 0; i < WIFI_MAX_HOSTS; i++) if (w->hosts[i].ip == ip) slot = i;
                for (int i = 0; slot < 0 && i < WIFI_MAX_HOSTS; i++) if (!w->hosts[i].ip) slot = i;
                for (int i = 0; slot < 0 && i < WIFI_MAX_HOSTS; i++) if ((uint32_t)(w->now() - w->hosts[i].seen) > 3000) slot = i;
                if (slot >= 0) { w->hosts[slot].ip = ip; memcpy(w->hosts[slot].name, d + 3, 15); w->hosts[slot].name[15] = 0; w->hosts[slot].seen = w->now(); }
            } else if (d[2] == H_JOIN && w->mode == WIFI_HOSTING && (!w->peer_ip || w->peer_ip == ip)) {
                w->peer_ip = ip; w->joined = 1; hello(w, ip, H_JOINOK);
            } else if (d[2] == H_JOINOK && w->mode == WIFI_JOINING && ip == w->peer_ip) w->joined = 1;
        }
    }
    return 0;
}

void wifi_tick(wifi_t *w) {
    uint8_t scratch[32]; uint32_t now = w->now();
    if (w->fd < 0) return;
    pump(w, scratch, sizeof scratch);                      /* (a game packet read here is dropped: the sender repeats it) */
    if (w->mode == WIFI_HOSTING && !w->joined && (uint32_t)(now - w->last_ann) >= 500) {
        hello(w, htonl(0xffffffffu), H_ANN);               /* limited broadcast ... */
#ifndef WIFI_HOST_TEST
        { uint32_t ip, mask, gw, dns; wup_net_info(&ip, &mask, &gw, &dns); if (ip) hello(w, ip | ~mask, H_ANN); }   /* ... and this subnet's */
#endif
        w->last_ann = now;
    } else if (w->mode == WIFI_JOINING && !w->joined && (uint32_t)(now - w->last_ann) >= 400) { hello(w, w->peer_ip, H_JOIN); w->last_ann = now; }
}

static int t_send(void *c, const uint8_t *d, int n) {
    wifi_t *w = c; if (w->fd < 0 || !w->peer_ip) return -1;
    send_to(w, w->peer_ip, d, n); return n;
}
static int t_recv(void *c, uint8_t *d, int max) { wifi_t *w = c; return w->fd < 0 ? 0 : pump(w, d, max); }
transport_t wifi_transport(wifi_t *w) { transport_t t = { t_send, t_recv, w }; return t; }
