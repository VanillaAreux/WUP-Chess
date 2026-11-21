/* ai: a small chess engine for single player (iterative-deepening alpha-beta, quiescence, piece-square eval).
 * Pure C on top of chess.h; no libwup dependency. Uses ~30 KB of static memory. */
#pragma once
#include "chess.h"

enum { AI_EASY = 0, AI_MEDIUM = 1, AI_HARD = 2 };
extern uint32_t (*ai_clock_ms)(void);   /* set before ai_best: milliseconds (for the time limit) */
extern void (*ai_yield)(void);          /* optional: called now and then during a long search (let the firmware run) */
int ai_best(const pos_t *p, int level, move_t *out);   /* best move for the side to move: 1, or 0 if it has none */
int ai_eval(const pos_t *p);                           /* static evaluation in centipawns, for the side to move */
