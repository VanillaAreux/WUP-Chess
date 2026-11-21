#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ai.h"
static uint32_t ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
static void fen(pos_t *p, const char *s) {
    memset(p, 0, sizeof *p); int sq = 56;
    for (; *s && *s != ' '; s++) { if (*s == '/') sq -= 16; else if (*s >= '1' && *s <= '8') sq += *s - '0'; else { const char *pc = "PNBRQKpnbrqk"; p->b[sq++] = (uint8_t)(strchr(pc, *s) - pc + 1); } }
    s++; p->turn = *s == 'w' ? WHITE : BLACK; s += 2; for (; *s && *s != ' '; s++) p->castle |= *s=='K'?1:*s=='Q'?2:*s=='k'?4:*s=='q'?8:0;
    s++; p->ep = -1; p->fullmove = 1;
}
static const char *name(move_t m, char *b) { b[0] = (char)('a' + (m.from & 7)); b[1] = (char)('1' + (m.from >> 3)); b[2] = (char)('a' + (m.to & 7)); b[3] = (char)('1' + (m.to >> 3)); b[4] = 0; return b; }
int main(void) {
    ai_clock_ms = ms; int f = 0; pos_t p; move_t m; char b[8], san[12];
    fen(&p, "r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq -");           /* Qxf7# */
    ai_best(&p, AI_MEDIUM, &m); chess_san(&p, m, san); printf("mate in 1: %s (want Qxf7#)\n", san); f += strcmp(san, "Qxf7#");
    fen(&p, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - -");                                         /* Ra8# */
    ai_best(&p, AI_HARD, &m); chess_san(&p, m, san); printf("back rank: %s (want Ra8#)\n", san); f += strcmp(san, "Ra8#");
    fen(&p, "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq -");                  /* white is mated already? no: Qh4 is check; test a capture of a free queen */
    fen(&p, "rnb1kbnr/pppppppp/8/8/3q4/2N5/PPPPPPPP/R1BQKBNR w KQkq -");                   /* Nxd4? knight c3 does not attack d4; just ensure a legal move */
    ai_best(&p, AI_MEDIUM, &m); printf("legal move found: %d\n", chess_is_legal(&p, m)); f += !chess_is_legal(&p, m);
    fen(&p, "4k3/8/8/3q4/8/2N5/8/4K3 w - -");                                              /* Nxd5 wins the queen */
    ai_best(&p, AI_MEDIUM, &m); chess_san(&p, m, san); printf("free queen: %s (want Nxd5)\n", san); f += strcmp(san, "Nxd5+") && strcmp(san, "Nxd5");
    /* self-play: each level vs a random mover, and time per move */
    for (int lv = 0; lv < 3; lv++) {
        int wins = 0, draws = 0, losses = 0; uint32_t worst = 0, total = 0, moves = 0;
        for (int g = 0; g < (lv == 2 ? 3 : 6); g++) {
            chess_init(&p); int ai_white = g & 1; srand(g + 7 * lv);
            while (!chess_status(&p) && p.fullmove < 150) {
                move_t all[256], mv; int n = chess_moves(&p, all);
                if ((p.turn == WHITE) == ai_white) { uint32_t t0 = ms(); ai_best(&p, lv, &mv); uint32_t d = ms() - t0; if (d > worst) worst = d; total += d; moves++; if (!chess_is_legal(&p, mv)) { puts("ILLEGAL AI MOVE"); return 1; } }
                else mv = all[rand() % n];
                chess_play(&p, mv);
            }
            int st = chess_status(&p);
            if (st == ST_CHECKMATE) { if ((p.turn == WHITE) != ai_white) wins++; else losses++; } else draws++;
        }
        printf("level %d vs random: %d wins, %d draws, %d losses; avg %u ms/move, worst %u ms\n", lv, wins, draws, losses, moves ? total / moves : 0, worst);
        if (lv >= 1 && losses) f++;
    }
    puts(f ? "FAILURES" : "ALL OK"); return f;
}
