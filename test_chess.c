#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "chess.h"
static void fen(pos_t *p, const char *s) {
    memset(p, 0, sizeof *p); int sq = 56;
    for (; *s && *s != ' '; s++) {
        if (*s == '/') sq -= 16; else if (*s >= '1' && *s <= '8') sq += *s - '0';
        else { const char *pc = "PNBRQKpnbrqk"; p->b[sq++] = (uint8_t)(strchr(pc, *s) - pc + 1); }
    }
    s++; p->turn = *s == 'w' ? WHITE : BLACK; s += 2;
    for (; *s && *s != ' '; s++) p->castle |= *s=='K'?1:*s=='Q'?2:*s=='k'?4:*s=='q'?8:0;
    s++; p->ep = -1; if (*s != '-') { p->ep = (int8_t)((s[0]-'a') + (s[1]-'1')*8); }
    p->fullmove = 1;
}
static int fails;
static void check(const char *name, const char *f, int d, uint64_t want) {
    pos_t p; fen(&p, f); uint64_t got = chess_perft(&p, d);
    printf("%-10s depth %d: %llu %s\n", name, d, (unsigned long long)got, got == want ? "ok" : "FAIL"); fails += got != want;
}
int main(void) {
    pos_t p; chess_init(&p); printf("start perft4 = %llu\n", (unsigned long long)chess_perft(&p, 4)); if (chess_perft(&p,4)!=197281) fails++;
    check("startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", 5, 4865609);
    check("kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 4, 4085603);
    check("pos3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 5, 674624);
    check("pos4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -", 4, 422333);
    check("pos5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ -", 4, 2103487);
    /* game outcomes + SAN */
    chess_init(&p); const char *mv[] = {"f2f3","e7e5","g2g4","d8h4"}; char s[16];
    for (int i = 0; i < 4; i++) { move_t m = { (uint8_t)((mv[i][0]-'a')+(mv[i][1]-'1')*8), (uint8_t)((mv[i][2]-'a')+(mv[i][3]-'1')*8), 0 };
        if (!chess_is_legal(&p, m)) { puts("illegal?!"); fails++; break; } chess_san(&p, m, s); printf("%s ", s); chess_play(&p, m); }
    printf("-> status %d (want %d)\n", chess_status(&p), ST_CHECKMATE); fails += chess_status(&p) != ST_CHECKMATE;
    fen(&p, "7k/5Q2/6K1/8/8/8/8/8 b - -"); printf("stalemate status %d (want %d)\n", chess_status(&p), ST_STALEMATE); fails += chess_status(&p) != ST_STALEMATE;
    puts(fails ? "FAILURES" : "ALL OK"); return fails;
}
