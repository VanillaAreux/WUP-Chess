#include "chess.h"
#include <string.h>
#include <stddef.h>

static const int KN[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
static const int KG[8][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};

int chess_color(int pc) { return pc == EMPTY ? -1 : (pc >= BP ? BLACK : WHITE); }
static int kind(int pc) { return pc == EMPTY ? 0 : (pc >= BP ? pc - BP + 1 : pc); }   /* 1 P 2 N 3 B 4 R 5 Q 6 K */
static int mk(int k, int c) { return k + (c == BLACK ? 6 : 0); }

static uint32_t hash_pos(const pos_t *p) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 64; i++) { h ^= p->b[i]; h *= 16777619u; }
    h ^= p->turn; h *= 16777619u; h ^= p->castle; h *= 16777619u; h ^= (uint8_t)p->ep; h *= 16777619u;
    return h;
}

void chess_init(pos_t *p) {
    static const uint8_t back[8] = { WR, WN, WB, WQ, WK, WB, WN, WR };
    memset(p, 0, sizeof *p);
    for (int f = 0; f < 8; f++) {
        p->b[f] = back[f]; p->b[8 + f] = WP; p->b[48 + f] = BP; p->b[56 + f] = (uint8_t)(back[f] + 6);
    }
    p->turn = WHITE; p->castle = 15; p->ep = -1; p->fullmove = 1;
    p->hist[0] = hash_pos(p); p->nhist = 1;
}

/* is square sq attacked by color 'by'? */
static int attacked(const pos_t *p, int sq, int by) {
    int f = sq & 7, r = sq >> 3;
    int pr = by == WHITE ? r - 1 : r + 1;                         /* a pawn of 'by' attacks from one rank behind */
    if (pr >= 0 && pr < 8) for (int d = -1; d <= 1; d += 2) {
        int pf = f + d; if (pf >= 0 && pf < 8 && p->b[pr * 8 + pf] == mk(1, by)) return 1;
    }
    for (int i = 0; i < 8; i++) {
        int nf = f + KN[i][0], nr = r + KN[i][1];
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8 && p->b[nr * 8 + nf] == mk(2, by)) return 1;
        nf = f + KG[i][0]; nr = r + KG[i][1];
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8 && p->b[nr * 8 + nf] == mk(6, by)) return 1;
    }
    for (int i = 0; i < 8; i++) {                                 /* sliders: KG[i] even index = orthogonal... use diag flag */
        int df = KG[i][0], dr = KG[i][1], diag = df && dr;
        int nf = f + df, nr = r + dr;
        while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
            int pc = p->b[nr * 8 + nf];
            if (pc) {
                if (chess_color(pc) == by) { int k = kind(pc); if (k == 5 || (diag ? k == 3 : k == 4)) return 1; }
                break;
            }
            nf += df; nr += dr;
        }
    }
    return 0;
}

static int king_sq(const pos_t *p, int c) { for (int i = 0; i < 64; i++) if (p->b[i] == mk(6, c)) return i; return -1; }
int chess_in_check(const pos_t *p, int c) { int k = king_sq(p, c); return k >= 0 && attacked(p, k, !c); }

static void apply(pos_t *p, move_t m) {                           /* no legality, no bookkeeping beyond the board, castling rights, ep */
    int pc = p->b[m.from], c = chess_color(pc), k = kind(pc), cap = p->b[m.to];
    int8_t new_ep = -1;
    p->b[m.from] = EMPTY;
    if (k == 1 && m.to == p->ep) p->b[m.to + (c == WHITE ? -8 : 8)] = EMPTY;      /* en passant capture */
    if (k == 1 && (m.to - m.from == 16 || m.from - m.to == 16)) new_ep = (int8_t)((m.from + m.to) / 2);
    if (k == 6 && (m.to - m.from == 2)) { p->b[m.from + 1] = p->b[m.from + 3]; p->b[m.from + 3] = EMPTY; }   /* castling rook */
    if (k == 6 && (m.from - m.to == 2)) { p->b[m.from - 1] = p->b[m.from - 4]; p->b[m.from - 4] = EMPTY; }
    if (k == 1 && m.promo) { static const uint8_t pk[5] = { 0, 2, 3, 4, 5 }; p->b[m.to] = (uint8_t)mk(pk[m.promo], c); }
    else p->b[m.to] = (uint8_t)pc;
    if (k == 6) p->castle &= (uint8_t)(c == WHITE ? ~3 : ~12);
    if (m.from == 0 || m.to == 0) p->castle &= (uint8_t)~2;
    if (m.from == 7 || m.to == 7) p->castle &= (uint8_t)~1;
    if (m.from == 56 || m.to == 56) p->castle &= (uint8_t)~8;
    if (m.from == 63 || m.to == 63) p->castle &= (uint8_t)~4;
    p->ep = new_ep; (void)cap;
}

static int legal_after(const pos_t *p, move_t m) {
    pos_t q; memcpy(&q, p, offsetof(pos_t, hist)); int c = p->turn; apply(&q, m); return !chess_in_check(&q, c);   /* (the board only: no history) */
}

static int add(const pos_t *p, move_t *out, int n, int from, int to, int promo) {
    move_t m = { (uint8_t)from, (uint8_t)to, (uint8_t)promo };
    if (legal_after(p, m)) out[n++] = m;
    return n;
}

static int gen(const pos_t *p, int only_from, move_t *out) {
    int n = 0, c = p->turn;
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->b[sq]; if (!pc || chess_color(pc) != c) continue;
        if (only_from >= 0 && sq != only_from) continue;
        int k = kind(pc), f = sq & 7, r = sq >> 3;
        if (k == 1) {
            int dir = c == WHITE ? 1 : -1, start = c == WHITE ? 1 : 6, last = c == WHITE ? 7 : 0;
            int nr = r + dir;
            if (nr < 0 || nr > 7) continue;
            int to = nr * 8 + f;
            if (!p->b[to]) {
                if (nr == last) for (int pr = PROMO_N; pr <= PROMO_Q; pr++) n = add(p, out, n, sq, to, pr);
                else {
                    n = add(p, out, n, sq, to, 0);
                    if (r == start && !p->b[to + dir * 8]) n = add(p, out, n, sq, to + dir * 8, 0);
                }
            }
            for (int d = -1; d <= 1; d += 2) {
                int nf = f + d; if (nf < 0 || nf > 7) continue;
                int t = nr * 8 + nf;
                if ((p->b[t] && chess_color(p->b[t]) != c) || t == p->ep) {
                    if (nr == last) for (int pr = PROMO_N; pr <= PROMO_Q; pr++) n = add(p, out, n, sq, t, pr);
                    else n = add(p, out, n, sq, t, 0);
                }
            }
        } else if (k == 2 || k == 6) {
            for (int i = 0; i < 8; i++) {
                int nf = f + (k == 2 ? KN[i][0] : KG[i][0]), nr = r + (k == 2 ? KN[i][1] : KG[i][1]);
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                int t = nr * 8 + nf;
                if (p->b[t] && chess_color(p->b[t]) == c) continue;
                n = add(p, out, n, sq, t, 0);
            }
            if (k == 6) {
                int home = c == WHITE ? 4 : 60, sh = c == WHITE ? 1 : 4, lg = c == WHITE ? 2 : 8;
                if (sq == home && !attacked(p, home, !c)) {
                    if ((p->castle & sh) && !p->b[home + 1] && !p->b[home + 2] && p->b[home + 3] == mk(4, c)
                        && !attacked(p, home + 1, !c)) n = add(p, out, n, sq, home + 2, 0);
                    if ((p->castle & lg) && !p->b[home - 1] && !p->b[home - 2] && !p->b[home - 3] && p->b[home - 4] == mk(4, c)
                        && !attacked(p, home - 1, !c)) n = add(p, out, n, sq, home - 2, 0);
                }
            }
        } else {
            for (int i = 0; i < 8; i++) {
                int df = KG[i][0], dr = KG[i][1], diag = df && dr;
                if (k == 3 && !diag) continue;
                if (k == 4 && diag) continue;
                int nf = f + df, nr = r + dr;
                while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                    int t = nr * 8 + nf;
                    if (p->b[t]) { if (chess_color(p->b[t]) != c) n = add(p, out, n, sq, t, 0); break; }
                    n = add(p, out, n, sq, t, 0);
                    nf += df; nr += dr;
                }
            }
        }
    }
    return n;
}

int chess_moves(const pos_t *p, move_t *out) { return gen(p, -1, out); }
int chess_moves_from(const pos_t *p, int from, move_t *out) { return gen(p, from, out); }

int chess_needs_promo(const pos_t *p, int from, int to) {
    int pc = p->b[from]; return kind(pc) == 1 && ((to >> 3) == (chess_color(pc) == WHITE ? 7 : 0));
}

int chess_is_legal(const pos_t *p, move_t m) {
    move_t all[256]; int n = chess_moves_from(p, m.from, all);
    for (int i = 0; i < n; i++) if (all[i].to == m.to && all[i].promo == m.promo) return 1;
    return 0;
}

void chess_play(pos_t *p, move_t m) {
    int pc = p->b[m.from], cap = p->b[m.to] || (kind(pc) == 1 && m.to == p->ep);
    apply(p, m);
    if (kind(pc) == 1 || cap) { p->halfmove = 0; p->nhist = 0; } else p->halfmove++;
    if (p->turn == BLACK) p->fullmove++;
    p->turn = (uint8_t)!p->turn;
    if (p->nhist < 128) p->hist[p->nhist++] = hash_pos(p);
}

static int insufficient(const pos_t *p) {
    int minors = 0, bishops_w = 0, bishops_b = 0;
    for (int i = 0; i < 64; i++) {
        int k = kind(p->b[i]); if (!k || k == 6) continue;
        if (k == 1 || k == 4 || k == 5) return 0;
        minors++;
        if (k == 3) { if (((i & 7) + (i >> 3)) & 1) bishops_w++; else bishops_b++; }
    }
    if (minors <= 1) return 1;
    return minors == bishops_w + bishops_b && (bishops_w == 0 || bishops_b == 0);   /* only same-colour bishops */
}

int chess_status(const pos_t *p) {
    move_t m[256];
    if (chess_moves(p, m) == 0) return chess_in_check(p, p->turn) ? ST_CHECKMATE : ST_STALEMATE;
    if (insufficient(p)) return ST_DRAW_MATERIAL;
    if (p->halfmove >= 100) return ST_DRAW_50;
    int reps = 0; for (int i = 0; i < p->nhist; i++) if (p->hist[i] == p->hist[p->nhist - 1]) reps++;
    if (p->nhist && reps >= 3) return ST_DRAW_REP;
    return ST_PLAYING;
}

void chess_san(const pos_t *p, move_t m, char *o) {
    int pc = p->b[m.from], k = kind(pc), cap = p->b[m.to] || (k == 1 && m.to == p->ep), n = 0;
    static const char L[7] = " PNBRQK";
    if (k == 6 && (m.to - m.from == 2 || m.from - m.to == 2)) {
        const char *s = m.to > m.from ? "O-O" : "O-O-O"; while (*s) o[n++] = *s++;
    } else {
        if (k != 1) {
            o[n++] = L[k];
            move_t all[256]; int cnt = chess_moves(p, all), amb = 0, samef = 0, samer = 0;
            for (int i = 0; i < cnt; i++) if (all[i].to == m.to && all[i].from != m.from && p->b[all[i].from] == pc) {
                amb = 1; if ((all[i].from & 7) == (m.from & 7)) samef = 1; if ((all[i].from >> 3) == (m.from >> 3)) samer = 1;
            }
            if (amb) { if (!samef) o[n++] = (char)('a' + (m.from & 7)); else if (!samer) o[n++] = (char)('1' + (m.from >> 3)); else { o[n++] = (char)('a' + (m.from & 7)); o[n++] = (char)('1' + (m.from >> 3)); } }
        } else if (cap) o[n++] = (char)('a' + (m.from & 7));
        if (cap) o[n++] = 'x';
        o[n++] = (char)('a' + (m.to & 7)); o[n++] = (char)('1' + (m.to >> 3));
        if (m.promo) { o[n++] = '='; o[n++] = "?NBRQ"[m.promo]; }
    }
    pos_t q = *p; chess_play(&q, m);
    if (chess_in_check(&q, q.turn)) { move_t t[256]; o[n++] = chess_moves(&q, t) ? '+' : '#'; }
    o[n] = 0;
}

uint64_t chess_perft(const pos_t *p, int depth) {
    move_t m[256]; int n = chess_moves(p, m); if (depth <= 1) return (uint64_t)n;
    uint64_t t = 0; for (int i = 0; i < n; i++) { pos_t q = *p; chess_play(&q, m[i]); t += chess_perft(&q, depth - 1); }
    return t;
}
