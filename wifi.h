/* wifi: UDP transport for rlink on the GamePad's Wi-Fi, plus LAN discovery (host announces, joiner picks). */
#pragma once
#include <stdint.h>
#include <netinet/in.h>
#include "rlink.h"

#define WIFI_PORT 28810
#define WIFI_MAX_HOSTS 4
enum { WIFI_IDLE = 0, WIFI_HOSTING, WIFI_SCANNING, WIFI_JOINING };

typedef struct { uint32_t ip; char name[16]; uint32_t seen; } wifi_host_t;
typedef struct {
    int fd, mode, joined;
    uint32_t peer_ip;                       /* network byte order, 0 = none yet */
    uint32_t last_ann;                      /* last announce / join attempt (ms) */
    char name[16];
    wifi_host_t hosts[WIFI_MAX_HOSTS];
    uint32_t (*now)(void);
} wifi_t;

int  wifi_open(wifi_t *w, uint32_t (*now)(void));      /* joins the menu's Wi-Fi and opens the socket: 0, or < 0 (no Wi-Fi) */
void wifi_close(wifi_t *w);
void wifi_host(wifi_t *w);                             /* announce this pad until someone joins */
void wifi_scan(wifi_t *w);                             /* listen for announcing pads */
int  wifi_hosts(wifi_t *w, wifi_host_t *out, int max); /* pads heard in the last 3 s */
void wifi_join(wifi_t *w, uint32_t ip);                /* ask that pad to start a game */
void wifi_tick(wifi_t *w);                             /* every few ms in the lobby: announce / retry / listen */
int  wifi_connected(const wifi_t *w);                  /* host: a joiner arrived; joiner: the host answered */
transport_t wifi_transport(wifi_t *w);                 /* for rlink_init */
const char *wifi_ip_str(void);
