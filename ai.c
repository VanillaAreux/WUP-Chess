#include "ai.h"
#include <string.h>
#include <stddef.h>

uint32_t (*ai_clock_ms)(void); void (*ai_yield)(void);

#define MATE 30000
#define INF  32000
#define MAXPLY 12

static const int VAL[7] = { 0, 100, 320, 330, 500, 900, 0 };
static const int8_t PST[7][64] = {
    {0},
    { 0,0,0,0,0,0,0,0, 50,50,50,50,50,50,50,50, 10,10,20,30,30,20,10,10, 5,5,10,25,25,10,5,5,
      0,0,0,20,20,0,0,0, 5,-5,-10,0,0,-10,-5,5, 5,10,10,-20,-20,10,10,5, 0,0,0,0,0,0,0,0 },
    { -50,-40,-30,-30,-30,-30,-40,-50, -40,-20,0,0,0,0,-20,-40, -30,0,10,15,15,10,0,-30, -30,5,15,20,20,15,5,-30,
      -30,0,15,20,20,15,0,-30, -30,5,10,15,15,10,5,-30, -40,-20,0,5,5,0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50 },
    { -20,-10,-10,-10,-10,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,10,10,5,0,-10, -10,5,5,10,10,5,5,-10,
      -10,0,10,10,10,10,0,-10, -10,10,10,10,10,10,10,-10, -10,5,0,0,0,0,5,-10, -20,-10,-10,-10,-10,-10,-10,-20 },
    { 0,0,0,0,0,0,0,0, 5,10,10,10,10,10,10,5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5,
      -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, 0,0,0,5,5,0,0,0 },
    { -20,-10,-10,-5,-5,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,5,5,5,0,-10, -5,0,5,5,5,5,0,-5,
      0,0,5,5,5,5,0,-5, -10,5,5,5,5,5,0,-10, -10,0,5,0,0,0,0,-10, -20,-10,-10,-5,-5,-10,-10,-20 },
    { -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
      -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10, 20,20,0,0,0,0,20,20, 20,30,10,0,0,10,30,20 },
};

static int kind(int pc) { return pc >= BP ? pc - BP + 1 : pc; }

int ai_eval(const pos_t *p) {
    int s = 0;
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->b[sq]; if (!pc) continue;
        int k = kind(pc);
        if (pc < BP) s += VAL[k] + PST[k][(7 - (sq >> 3)) * 8 + (sq & 7)];
        else         s -= VAL[k] + PST[k][(sq >> 3) * 8 + (sq & 7)];
    }
    return p->turn == WHITE ? s : -s;
}

/* ---- search (all per-ply storage is static: nothing big on the app's small stack) ---- */
static pos_t sp[MAXPLY]; static move_t sm[MAXPLY][256]; static int16_t ss[MAXPLY][256];
static uint32_t nodes, t_end; static int stopped;
static uint32_t rng = 2463534242u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void child(int ply, move_t m) {
    memcpy(&sp[ply + 1], &sp[ply], offsetof(pos_t, hist)); sp[ply + 1].nhist = 0; chess_play(&sp[ply + 1], m);
}
static int is_capture(const pos_t *p, move_t m) { return p->b[m.to] || (kind(p->b[m.from]) == 1 && m.to == p->ep); }

static void score_moves(int ply, int n, int only_loud, move_t first) {
    const pos_t *p = &sp[ply];
    for (int i = 0; i < n; i++) {
        move_t m = sm[ply][i]; int v = 0;
        if (is_capture(p, m)) { int vic = p->b[m.to] ? kind(p->b[m.to]) : 1; v = 1000 + vic * 10 - kind(p->b[m.from]); }
        if (m.promo) v += 800 + m.promo;
        if (m.from == first.from && m.to == first.to && m.promo == first.promo && first.from != first.to) v += 5000;
        ss[ply][i] = (int16_t)(only_loud && v < 1000 ? -1 : v);
    }
}
static int pick(int ply, int from, int n) {                    /* best remaining move, swapped to position 'from' */
    int bi = from; for (int i = from + 1; i < n; i++) if (ss[ply][i] > ss[ply][bi]) bi = i;
    if (bi != from) { move_t t = sm[ply][from]; sm[ply][from] = sm[ply][bi]; sm[ply][bi] = t; int16_t u = ss[ply][from]; ss[ply][from] = ss[ply][bi]; ss[ply][bi] = u; }
    return ss[ply][from];
}
static void tick(void) {
    if ((++nodes & 1023) == 0) {
        if (ai_clock_ms && (int32_t)(ai_clock_ms() - t_end) >= 0) stopped = 1;
        if (ai_yield) ai_yield();
    }
}

static int qsearch(int ply, int alpha, int beta) {
    tick(); if (stopped) return 0;
    const pos_t *p = &sp[ply]; int stand = ai_eval(p);
    if (ply >= MAXPLY - 1 || stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    int n = chess_moves(p, sm[ply]); score_moves(ply, n, 1, (move_t){ 0, 0, 0 });
    for (int i = 0; i < n; i++) {
        if (pick(ply, i, n) < 0) break;
        child(ply, sm[ply][i]);
        int v = -qsearch(ply + 1, -beta, -alpha); if (stopped) return 0;
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

static int negamax(int ply, int depth, int alpha, int beta) {
    tick(); if (stopped) return 0;
    const pos_t *p = &sp[ply];
    if (p->halfmove >= 100) return 0;
    if (depth <= 0 || ply >= MAXPLY - 2) return qsearch(ply, alpha, beta);
    int n = chess_moves(p, sm[ply]);
    if (!n) return chess_in_check(p, p->turn) ? -MATE + ply : 0;
    score_moves(ply, n, 0, (move_t){ 0, 0, 0 });
    int best = -INF;
    for (int i = 0; i < n; i++) {
        pick(ply, i, n); child(ply, sm[ply][i]);
        int v = -negamax(ply + 1, depth - 1, -beta, -alpha); if (stopped) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

int ai_best(const pos_t *p, int level, move_t *out) {
    static const int maxd[3] = { 2, 3, 6 }; static const uint32_t ms[3] = { 500, 2000, 5000 };
    memcpy(&sp[0], p, offsetof(pos_t, hist)); sp[0].nhist = 0;
    int n = chess_moves(&sp[0], sm[0]); if (!n) return 0;
    move_t root[256]; memcpy(root, sm[0], sizeof(move_t) * (size_t)n);
    if (ai_clock_ms) { rng ^= ai_clock_ms() * 2654435761u; if (!rng) rng = 1; t_end = ai_clock_ms() + ms[level]; }
    nodes = 0; stopped = 0;
    move_t best = root[0]; int best_score = -INF;
    for (int d = 1; d <= maxd[level]; d++) {
        int alpha = -INF, cur_score = -INF; move_t cur = root[0]; int done = 1;
        for (int i = 0; i < n; i++) {
            memcpy(&sp[0], p, offsetof(pos_t, hist)); sp[0].nhist = 0;
            child(0, root[i]);
            int v = -negamax(1, d - 1, -INF, level == AI_EASY ? INF : -alpha);        /* easy: every move gets a full score */
            if (stopped) { done = 0; break; }
            if (level == AI_EASY) v += (int)(rnd() % 161) - 80;                       /* ... and some noise: it blunders */
            if (v > cur_score) { cur_score = v; cur = root[i]; }
            if (v > alpha && level != AI_EASY) alpha = v;
        }
        if (!done) break;
        best = cur; best_score = cur_score;
        for (int i = 0; i < n; i++) if (root[i].from == best.from && root[i].to == best.to && root[i].promo == best.promo) { move_t t = root[0]; root[0] = root[i]; root[i] = t; break; }
        if (best_score > MATE - 100) break;                                           /* a forced mate: no need to look deeper */
    }
    (void)best_score; *out = best; return 1;
}
