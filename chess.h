/* chess.h: rules of chess, no dependencies (plain C99). */
#pragma once
#include <stdint.h>

enum { EMPTY = 0, WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK };
enum { WHITE = 0, BLACK = 1 };
enum { PROMO_NONE = 0, PROMO_N, PROMO_B, PROMO_R, PROMO_Q };
enum { ST_PLAYING = 0, ST_CHECKMATE, ST_STALEMATE, ST_DRAW_50, ST_DRAW_MATERIAL, ST_DRAW_REP };

/* squares 0..63: a1 = 0, b1 = 1, ... h8 = 63 (file = sq & 7, rank = sq >> 3) */
typedef struct { uint8_t from, to, promo; } move_t;

typedef struct {
    uint8_t b[64];
    uint8_t turn;          /* WHITE / BLACK to move */
    uint8_t castle;        /* 1 = white short, 2 = white long, 4 = black short, 8 = black long */
    int8_t ep;             /* en passant target square, or -1 */
    uint8_t halfmove;      /* plies since a pawn move or capture */
    uint16_t fullmove;
    uint32_t hist[128];    /* position hashes for repetition (kept since the last irreversible move) */
    uint8_t nhist;
} pos_t;

void chess_init(pos_t *p);
int  chess_color(int piece);                              /* WHITE/BLACK, or -1 for EMPTY */
int  chess_moves(const pos_t *p, move_t *out);            /* all legal moves: how many (max 256) */
int  chess_moves_from(const pos_t *p, int from, move_t *out);
int  chess_in_check(const pos_t *p, int color);
int  chess_is_legal(const pos_t *p, move_t m);            /* promo may be PROMO_NONE for non-promotions */
void chess_play(pos_t *p, move_t m);                      /* m must be legal */
int  chess_status(const pos_t *p);                        /* ST_* for the side to move */
int  chess_needs_promo(const pos_t *p, int from, int to); /* a pawn reaching the last rank */
void chess_san(const pos_t *p, move_t m, char *out);      /* e.g. "Nf3", "exd5", "O-O", "e8=Q+" (out >= 12 bytes); call before chess_play */
uint64_t chess_perft(const pos_t *p, int depth);
