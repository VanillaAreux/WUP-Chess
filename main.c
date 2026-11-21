/* WUP Chess: chess for the Wii U GamePad (libwup). Play on one pad, or against another GamePad over Wi-Fi.
 * Touch a piece and then its square, or use D-pad + A. X resigns, Y offers a draw, B backs out. */
#include "wup.h"
#include "chess.h"
#include "rlink.h"
#include "wifi.h"
#include "ai.h"
#include <stdio.h>
#include <string.h>

WUP_APP("WUP Chess");

#define BX 16
#define BY 16
#define SQ 56
#define PX 490
#define PW 348

enum { S_MENU, S_HOST, S_JOIN, S_GAME, S_LEVEL };
enum { D_NONE, D_PROMO, D_LEAVE, D_RESIGN, D_DRAW_IN };
enum { A_NONE, A_HOST, A_JOIN, A_LOCAL, A_QUIT, A_BACK, A_RESIGN, A_DRAW, A_MENU, A_REMATCH, A_YES, A_NO,
       A_PROMO_Q, A_PROMO_R, A_PROMO_B, A_PROMO_N, A_PICK0, A_PICK1, A_PICK2, A_PICK3, A_ACCEPT, A_DECLINE,
       A_CPU, A_COLOR, A_EASY, A_MED, A_HARD };

/* ---------- clock ---------- */
static uint64_t us64; static uint32_t lastus;
static uint32_t now_ms(void) { uint32_t u = wup_us(); us64 += (uint32_t)(u - lastus); lastus = u; return (uint32_t)(us64 / 1000); }

/* ---------- state ---------- */
static int screen = S_MENU, dlg = D_NONE, dirty = 1, quit_app;
static pos_t pos;
static int online, is_host, my_color = WHITE;
static int sel = -1, cursor = 12, last_from = -1, last_to = -1;
static move_t lm[64]; static int nlm;
static char san[600][12]; static int nply;
static int game_over; static char result[48];
static move_t pend;
static int draw_out, rematch_me, rematch_peer;
static uint32_t game_t0, note_until; static char note[80];
static int conn_drawn = 1;
static int vs_cpu, cpu_color = BLACK, level = AI_MEDIUM, pref_color = WHITE; static uint32_t cpu_wait;
static void ai_nap(void) { wup_sleep(1); }
static wifi_t wifi; static rlink_t rl; static int wifi_up;
static wifi_host_t found[WIFI_MAX_HOSTS]; static int nfound;

/* ---------- colours ---------- */
#define C_BG_TOP C24(28, 34, 52)
#define C_BG_BOT C24(10, 12, 22)
#define C_LIGHT RGB(240, 217, 181)
#define C_DARK  RGB(181, 136, 99)
#define C_TEXT  RGB(240, 244, 255)
#define C_DIM   RGB(150, 160, 185)
#define C_ACC   RGB(90, 150, 255)
#define C_RED   RGB(235, 90, 90)

/* ---------- buttons ---------- */
typedef struct { int x, y, w, h, act; char label[28]; } btn_t;
static btn_t btns[10]; static int nb, focus, cancel_act;
static void btn_reset(int cancel) { nb = 0; focus = 0; cancel_act = cancel; }
static void btn_add(int x, int y, int w, int h, int act, const char *label) {
    if (nb >= 10) return;
    btn_t *b = &btns[nb++]; b->x = x; b->y = y; b->w = w; b->h = h; b->act = act;
    strncpy(b->label, label, sizeof b->label - 1); b->label[sizeof b->label - 1] = 0;
}
static void btn_draw_all(void) {
    for (int i = 0; i < nb; i++) {
        btn_t *b = &btns[i];
        gfx_rrect(b->x + 2, b->y + 4, b->w, b->h, 12, RGB(0, 0, 0), 90);
        gfx_rrect(b->x, b->y, b->w, b->h, 12, i == focus ? C_ACC : RGB(66, 74, 96), 256);
        gfx_text_c(&font_label, b->x + b->w / 2, b->y + b->h / 2 + 8, b->label, C_TEXT);
    }
}

/* ---------- board geometry ---------- */
static int flipped(void) { return (online || vs_cpu) && my_color == BLACK; }
static void sq_xy(int sq, int *x, int *y) {
    int f = sq & 7, r = sq >> 3, fl = flipped();
    *x = BX + (fl ? 7 - f : f) * SQ; *y = BY + (fl ? r : 7 - r) * SQ;
}
static int xy_sq(int x, int y) {
    if (x < BX || y < BY || x >= BX + 8 * SQ || y >= BY + 8 * SQ) return -1;
    int col = (x - BX) / SQ, row = (y - BY) / SQ, fl = flipped();
    return (fl ? row : 7 - row) * 8 + (fl ? 7 - col : col);
}

/* ---------- game flow ---------- */
static void set_note(const char *s) { strncpy(note, s, sizeof note - 1); note[sizeof note - 1] = 0; note_until = now_ms() + 4000; dirty = 1; }

static void new_game(void) {
    chess_init(&pos); sel = -1; nlm = 0; nply = 0; game_over = 0; result[0] = 0; dlg = D_NONE;
    last_from = last_to = -1; draw_out = rematch_me = rematch_peer = 0; cpu_wait = 0;
    cursor = my_color == WHITE ? 12 : 52; game_t0 = now_ms(); screen = S_GAME; dirty = 1;
}
static void end_game(const char *s) { game_over = 1; strncpy(result, s, sizeof result - 1); result[sizeof result - 1] = 0; sel = -1; nlm = 0; dlg = D_NONE; dirty = 1; }

static void check_end(void) {
    int st = chess_status(&pos); if (!st) return;
    if (st == ST_CHECKMATE) {
        int winner = !pos.turn;
        if (vs_cpu) end_game(winner == my_color ? "Checkmate - you win!" : "Checkmate - the computer wins");
        else if (online) end_game(winner == my_color ? "Checkmate - you win!" : "Checkmate - you lose");
        else end_game(winner == WHITE ? "Checkmate - White wins" : "Checkmate - Black wins");
    } else end_game(st == ST_STALEMATE ? "Draw by stalemate" : st == ST_DRAW_50 ? "Draw: 50-move rule" :
                    st == ST_DRAW_MATERIAL ? "Draw: not enough pieces" : "Draw by repetition");
}

static void commit_move(move_t m, int remote) {
    if (nply < 600) chess_san(&pos, m, san[nply++]);
    chess_play(&pos, m); last_from = m.from; last_to = m.to; sel = -1; nlm = 0; draw_out = 0;
    if (dlg == D_DRAW_IN) dlg = D_NONE;
    if (!remote && online) { msg_t mm = { MSG_MOVE, m.from, m.to, m.promo }; rlink_send(&rl, mm); }
    check_end(); dirty = 1;
}

static int can_move(void) { return !game_over && dlg == D_NONE && ((!online && !vs_cpu) || pos.turn == my_color); }

static void select_square(int sq) {
    if (sq < 0 || !can_move()) return;
    if (sel >= 0) {
        for (int i = 0; i < nlm; i++) if (lm[i].to == sq) {
            if (chess_needs_promo(&pos, sel, sq)) { pend.from = (uint8_t)sel; pend.to = (uint8_t)sq; pend.promo = 0; dlg = D_PROMO; dirty = 1; return; }
            commit_move(lm[i], 0); return;
        }
    }
    int pc = pos.b[sq];
    if (pc && chess_color(pc) == pos.turn && sq != sel) { sel = sq; nlm = chess_moves_from(&pos, sq, lm); }
    else { sel = -1; nlm = 0; }
    dirty = 1;
}

static void to_menu(void) {
    if (wifi_up) { wifi_close(&wifi); wifi_up = 0; }
    online = 0; vs_cpu = 0; screen = S_MENU; dlg = D_NONE; dirty = 1;
}

static void start_online(int host) {
    if (wifi_open(&wifi, now_ms) < 0) { wifi_up = 0; set_note("Connect to Wi-Fi first: Homebrew Menu > Settings > Network"); return; }
    wifi_up = 1; is_host = host;
    if (host) wifi_host(&wifi); else wifi_scan(&wifi);
    screen = host ? S_HOST : S_JOIN; dirty = 1;
}

static void connected(void) {
    rlink_init(&rl, wifi_transport(&wifi), now_ms);
    online = 1; my_color = is_host ? WHITE : BLACK; new_game();
}

static void handle_msg(msg_t m) {
    switch (m.kind) {
    case MSG_MOVE: {
        move_t mv = { m.a, m.b, m.c };
        if (!game_over && pos.turn != my_color && m.a < 64 && m.b < 64 && chess_is_legal(&pos, mv)) commit_move(mv, 1);
        break; }
    case MSG_RESIGN: if (!game_over) end_game("Opponent resigned - you win!"); break;
    case MSG_DRAW_OFFER: if (!game_over && dlg == D_NONE) { dlg = D_DRAW_IN; dirty = 1; } break;
    case MSG_DRAW_ACCEPT: if (!game_over && draw_out) end_game("Draw agreed"); break;
    case MSG_DRAW_DECLINE: if (draw_out) { draw_out = 0; set_note("Draw declined"); } break;
    case MSG_REMATCH: rematch_peer = 1; if (rematch_me) { my_color = !my_color; new_game(); } else dirty = 1; break;
    }
}
static void send_kind(int kind) { msg_t m = { (uint8_t)kind, 0, 0, 0 }; rlink_send(&rl, m); }

static void do_act(int act) {
    dirty = 1;
    switch (act) {
    case A_HOST: start_online(1); break;
    case A_JOIN: start_online(0); break;
    case A_LOCAL: online = 0; vs_cpu = 0; my_color = WHITE; new_game(); break;
    case A_CPU: screen = S_LEVEL; break;
    case A_COLOR: pref_color = !pref_color; break;
    case A_EASY: case A_MED: case A_HARD:
        level = act - A_EASY; online = 0; vs_cpu = 1; my_color = pref_color; cpu_color = !my_color; new_game(); break;
    case A_QUIT: quit_app = 1; break;
    case A_BACK: case A_MENU: to_menu(); break;
    case A_PICK0: case A_PICK1: case A_PICK2: case A_PICK3:
        if (act - A_PICK0 < nfound) { wifi_join(&wifi, found[act - A_PICK0].ip); is_host = 0; screen = S_JOIN; }
        break;
    case A_RESIGN: if (!game_over) dlg = D_RESIGN; break;
    case A_DRAW:
        if (game_over) break;
        if (vs_cpu) {                                              /* the computer agrees when it is worse off */
            int e = ai_eval(&pos); if (pos.turn != cpu_color) e = -e;
            if (e <= -150) end_game("Draw agreed"); else set_note("The computer declined the draw");
        } else if (!online) end_game("Draw agreed");
        else if (!draw_out) { draw_out = 1; send_kind(MSG_DRAW_OFFER); set_note("Draw offered"); }
        break;
    case A_YES:
        if (dlg == D_RESIGN) {
            if (online) { send_kind(MSG_RESIGN); end_game("You resigned"); }
            else if (vs_cpu) end_game("You resigned");
            else end_game(pos.turn == WHITE ? "White resigned - Black wins" : "Black resigned - White wins");
        } else if (dlg == D_LEAVE) to_menu();
        dlg = D_NONE;
        break;
    case A_NO: dlg = D_NONE; break;
    case A_ACCEPT: send_kind(MSG_DRAW_ACCEPT); end_game("Draw agreed"); break;
    case A_DECLINE: send_kind(MSG_DRAW_DECLINE); dlg = D_NONE; break;
    case A_REMATCH:
        if (vs_cpu) { my_color = !my_color; cpu_color = !my_color; new_game(); break; }
        if (!online) { my_color = WHITE; new_game(); break; }
        rematch_me = 1; send_kind(MSG_REMATCH);
        if (rematch_peer) { my_color = !my_color; new_game(); }
        break;
    case A_PROMO_Q: case A_PROMO_R: case A_PROMO_B: case A_PROMO_N: {
        pend.promo = act == A_PROMO_Q ? PROMO_Q : act == A_PROMO_R ? PROMO_R : act == A_PROMO_B ? PROMO_B : PROMO_N;
        dlg = D_NONE; commit_move(pend, 0); break; }
    }
}

/* ---------- drawing ---------- */
static void draw_piece(int pc, int cx, int cy) {
    static const char L[7] = "?PNBRQK";
    int white = pc < BP, k = white ? pc : pc - 6; char s[2] = { L[k], 0 };
    gfx_circle(cx + 1, cy + 3, 23, RGB(0, 0, 0), 70);
    gfx_circle(cx, cy, 23, white ? RGB(60, 60, 70) : RGB(230, 230, 240), 256);
    gfx_circle(cx, cy, 21, white ? RGB(250, 250, 252) : RGB(38, 38, 48), 256);
    gfx_text_c(&font_title, cx, cy + 11, s, white ? RGB(40, 40, 50) : RGB(240, 240, 250));
}

static void draw_board(void) {
    int kcheck = chess_in_check(&pos, pos.turn) ? -2 : -1;
    if (kcheck == -2) for (int i = 0; i < 64; i++) if (pos.b[i] == (pos.turn == WHITE ? WK : BK)) kcheck = i;
    for (int sq = 0; sq < 64; sq++) {
        int x, y; sq_xy(sq, &x, &y);
        gfx_fill(x, y, SQ, SQ, (((sq & 7) + (sq >> 3)) & 1) ? C_LIGHT : C_DARK);
        if (sq == last_from || sq == last_to) gfx_fill_a(x, y, SQ, SQ, RGB(255, 235, 90), 110);
        if (sq == sel) gfx_fill_a(x, y, SQ, SQ, RGB(90, 200, 255), 150);
        if (sq == kcheck) gfx_fill_a(x, y, SQ, SQ, RGB(235, 50, 50), 150);
    }
    int fl = flipped();
    for (int i = 0; i < 8; i++) {                                 /* file and rank letters */
        int f = fl ? 7 - i : i, r = fl ? i : 7 - i; char s[2];
        s[1] = 0; s[0] = (char)('a' + f); gfx_text(&font_small, BX + i * SQ + SQ - 12, BY + 8 * SQ - 4, s, (((f + 0) & 1) ? C_DARK : C_LIGHT));
        s[0] = (char)('1' + r); gfx_text(&font_small, BX + 3, BY + i * SQ + 16, s, (((r + 0) & 1) ? C_LIGHT : C_DARK));
    }
    for (int sq = 0; sq < 64; sq++) if (pos.b[sq]) { int x, y; sq_xy(sq, &x, &y); draw_piece(pos.b[sq], x + SQ / 2, y + SQ / 2); }
    for (int i = 0; i < nlm; i++) {                               /* where the selected piece can go */
        int x, y; sq_xy(lm[i].to, &x, &y);
        if (pos.b[lm[i].to] || (lm[i].to == pos.ep && pos.b[lm[i].from] % 6 == WP)) gfx_rrect_outline(x + 2, y + 2, SQ - 4, SQ - 4, 8, 4, RGB(220, 60, 60));
        else gfx_circle(x + SQ / 2, y + SQ / 2, 8, RGB(40, 120, 60), 200);
    }
    { int x, y; sq_xy(cursor, &x, &y); gfx_rrect_outline(x, y, SQ, SQ, 4, 3, C_ACC); }
}

static void side_label(int y, int color, const char *tag) {
    char s[40]; snprintf(s, sizeof s, "%s%s", color == WHITE ? "White" : "Black", tag);
    gfx_circle(PX + 14, y - 7, 11, color == WHITE ? RGB(250, 250, 252) : RGB(38, 38, 48), 256);
    gfx_rrect_outline(PX + 3, y - 18, 22, 22, 11, 2, RGB(120, 130, 150));
    gfx_text(&font_label, PX + 36, y, s, C_TEXT);
}

static int conn_ok(void) { return !online || (uint32_t)(now_ms() - game_t0) < 4000 || rlink_alive(&rl); }

static void draw_game_panel(void) {
    int bottom = flipped() ? BLACK : WHITE, top = !bottom;
    side_label(40, top, vs_cpu ? "  (computer)" : online ? "  (opponent)" : ""); side_label(462, bottom, online || vs_cpu ? "  (you)" : "");
    char st[56];
    if (game_over) snprintf(st, sizeof st, "%s", result);
    else if (vs_cpu) snprintf(st, sizeof st, "%s%s", pos.turn == cpu_color ? "Computer is thinking" : "Your move", pos.turn != cpu_color && chess_in_check(&pos, pos.turn) ? " - check" : "");
    else if (online && !conn_ok()) snprintf(st, sizeof st, "Connection lost");
    else if (online) snprintf(st, sizeof st, "%s%s", pos.turn == my_color ? "Your move" : "Opponent's move", chess_in_check(&pos, pos.turn) ? " - check" : "");
    else snprintf(st, sizeof st, "%s to move%s", pos.turn == WHITE ? "White" : "Black", chess_in_check(&pos, pos.turn) ? " - check" : "");
    gfx_text(game_over ? &font_label : &font_title, PX, 92, st, game_over || (online && !conn_ok()) ? C_RED : C_TEXT);
    if (note[0] && now_ms() < note_until) gfx_text(&font_body, PX, 120, note, C_DIM);
    else if (rematch_peer && game_over) gfx_text(&font_body, PX, 120, "Opponent wants a rematch", C_DIM);
    else if (draw_out) gfx_text(&font_body, PX, 120, "Draw offer sent...", C_DIM);
    gfx_rrect(PX, 134, PW, 196, 10, RGB(0, 0, 0), 70);          /* move list */
    int first = nply / 2 - 6; if (first < 0) first = 0;
    for (int n = first, row = 0; n * 2 < nply && row < 7; n++, row++) {
        char ln[48]; snprintf(ln, sizeof ln, "%d.", n + 1);
        gfx_text(&font_body, PX + 12, 160 + row * 26, ln, C_DIM);
        gfx_text(&font_body, PX + 62, 160 + row * 26, san[n * 2], C_TEXT);
        if (n * 2 + 1 < nply) gfx_text(&font_body, PX + 190, 160 + row * 26, san[n * 2 + 1], C_TEXT);
    }
}

static void draw_dialog(const char *title, const char *line) {
    gfx_fill_a(0, 0, GFX_W, GFX_H, RGB(0, 0, 0), 150);
    gfx_shadow(227, 120, 400, 240, 16, 14, 160);
    gfx_rrect(227, 120, 400, 240, 16, RGB(38, 44, 62), 256);
    gfx_text_c(&font_title, GFX_W / 2, 175, title, C_TEXT);
    if (line) gfx_text_c(&font_body, GFX_W / 2, 210, line, C_DIM);
}

static void draw_game(void) {
    gfx_vgrad24(0, 0, GFX_W, GFX_H, C_BG_TOP, C_BG_BOT);
    draw_board(); draw_game_panel();
    btn_reset(A_NONE);
    if (dlg == D_NONE) {
        if (game_over) { btn_add(PX, 345, 166, 44, A_REMATCH, rematch_me ? "Waiting..." : vs_cpu ? "Play again" : "Rematch"); btn_add(PX + 182, 345, 166, 44, A_MENU, "Menu"); }
        else { btn_add(PX, 345, 166, 44, A_RESIGN, "Resign"); btn_add(PX + 182, 345, 166, 44, A_DRAW, online ? "Offer draw" : "Draw"); btn_add(PX, 399, 166, 44, A_MENU, "Menu"); }
        focus = -1;
        btn_draw_all();
        gfx_text(&font_small, PX + 182, 430, "X resign  Y draw  B menu", C_DIM);
    } else {
        if (dlg == D_PROMO) {
            draw_dialog("Promote to", 0); btn_reset(A_PROMO_Q);
            btn_add(247, 250, 85, 60, A_PROMO_Q, "Q"); btn_add(342, 250, 85, 60, A_PROMO_R, "R");
            btn_add(437, 250, 85, 60, A_PROMO_B, "B"); btn_add(532, 250, 85, 60, A_PROMO_N, "N");
        } else if (dlg == D_RESIGN) {
            draw_dialog("Resign this game?", "It counts as a loss."); btn_reset(A_NO);
            btn_add(257, 260, 170, 56, A_YES, "Resign"); btn_add(437, 260, 170, 56, A_NO, "Cancel"); focus = 1;
        } else if (dlg == D_LEAVE) {
            draw_dialog("Leave the game?", online ? "The game ends for both players." : "The game is not saved."); btn_reset(A_NO);
            btn_add(257, 260, 170, 56, A_YES, "Leave"); btn_add(437, 260, 170, 56, A_NO, "Stay"); focus = 1;
        } else if (dlg == D_DRAW_IN) {
            draw_dialog("Draw offered", "Your opponent proposes a draw."); btn_reset(A_DECLINE);
            btn_add(257, 260, 170, 56, A_ACCEPT, "Accept"); btn_add(437, 260, 170, 56, A_DECLINE, "Decline");
        }
        btn_draw_all();
    }
}

static void draw_menu(void) {
    gfx_vgrad24(0, 0, GFX_W, GFX_H, C_BG_TOP, C_BG_BOT);
    gfx_text_c(&font_big, GFX_W / 2, 80, "WUP Chess", C_TEXT);
    if (note[0] && now_ms() < note_until) gfx_text_c(&font_body, GFX_W / 2, 114, note, C_RED);
    else gfx_text_c(&font_body, GFX_W / 2, 114, "Play the computer, a friend, or another GamePad", C_DIM);
    btn_reset(A_QUIT);
    btn_add(247, 130, 360, 54, A_CPU, "Play the computer"); btn_add(247, 190, 360, 54, A_HOST, "Host online game");
    btn_add(247, 250, 360, 54, A_JOIN, "Join online game"); btn_add(247, 310, 360, 54, A_LOCAL, "Two players, one pad");
    btn_add(247, 370, 360, 54, A_QUIT, "Quit");
    btn_draw_all();
    gfx_text_c(&font_small, GFX_W / 2, 462, "Touch, or D-pad + A     B: quit", C_DIM);
}

static void draw_level(void) {
    gfx_vgrad24(0, 0, GFX_W, GFX_H, C_BG_TOP, C_BG_BOT);
    gfx_text_c(&font_title, GFX_W / 2, 70, "Play the computer", C_TEXT);
    btn_reset(A_BACK);
    btn_add(247, 100, 360, 54, A_COLOR, pref_color == WHITE ? "You play: White" : "You play: Black");
    gfx_text_c(&font_body, GFX_W / 2, 190, "Choose a level to start", C_DIM);
    btn_add(247, 205, 360, 54, A_EASY, "Easy"); btn_add(247, 265, 360, 54, A_MED, "Medium"); btn_add(247, 325, 360, 54, A_HARD, "Hard");
    btn_add(327, 395, 200, 50, A_BACK, "Back");
    focus = 2; btn_draw_all();
    gfx_text_c(&font_small, GFX_W / 2, 468, "Hard thinks for a few seconds per move", C_DIM);
}

static void draw_lobby(void) {
    char s[64]; int dots = (int)(now_ms() / 400 % 4);
    gfx_vgrad24(0, 0, GFX_W, GFX_H, C_BG_TOP, C_BG_BOT);
    btn_reset(A_BACK);
    if (screen == S_HOST) {
        gfx_text_c(&font_title, GFX_W / 2, 120, "Hosting a game", C_TEXT);
        snprintf(s, sizeof s, "Waiting for an opponent%.*s", dots, "...");
        gfx_text_c(&font_label, GFX_W / 2, 190, s, C_TEXT);
        snprintf(s, sizeof s, "This pad: %s", wifi_ip_str()); gfx_text_c(&font_body, GFX_W / 2, 240, s, C_DIM);
        gfx_text_c(&font_body, GFX_W / 2, 275, "On the other pad choose \"Join online game\"", C_DIM);
        gfx_text_c(&font_small, GFX_W / 2, 305, "Both pads must be on the same Wi-Fi network. You play White.", C_DIM);
    } else if (wifi.mode == WIFI_JOINING) {
        gfx_text_c(&font_title, GFX_W / 2, 150, "Joining", C_TEXT);
        snprintf(s, sizeof s, "Waiting for the host%.*s", dots, "..."); gfx_text_c(&font_label, GFX_W / 2, 215, s, C_DIM);
    } else {
        gfx_text_c(&font_title, GFX_W / 2, 90, "Join a game", C_TEXT);
        nfound = wifi_hosts(&wifi, found, WIFI_MAX_HOSTS);
        if (!nfound) { snprintf(s, sizeof s, "Looking for hosts%.*s", dots, "..."); gfx_text_c(&font_label, GFX_W / 2, 200, s, C_DIM); }
        for (int i = 0; i < nfound; i++) {
            const uint8_t *p = (const uint8_t *)&found[i].ip; char t[40];
            snprintf(t, sizeof t, "%s  %d.%d.%d.%d", found[i].name, p[0], p[1], p[2], p[3]);
            btn_add(207, 130 + i * 62, 440, 52, A_PICK0 + i, t);
        }
    }
    btn_add(327, 400, 200, 52, A_BACK, "Back"); focus = nb - 1;
    btn_draw_all();
}

static void redraw(void) {
    if (screen == S_MENU) draw_menu(); else if (screen == S_LEVEL) draw_level(); else if (screen == S_GAME) draw_game(); else draw_lobby();
    gfx_present_all(); dirty = 0;
}

/* ---------- input ---------- */
static int tap_poll(int *x, int *y) {
    static int down, lx, ly; int tx, ty;
    if (wup_touch(&tx, &ty)) { down = 1; lx = tx; ly = ty; return 0; }
    if (down) { down = 0; *x = lx; *y = ly; return 1; }
    return 0;
}

static void on_tap(int x, int y) {
    for (int i = 0; i < nb; i++) {
        btn_t *b = &btns[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) { do_act(b->act); return; }
    }
    if (screen == S_GAME && dlg == D_NONE) select_square(xy_sq(x, y));
}

static void on_buttons(unsigned p) {
    if (screen == S_GAME && dlg == D_NONE) {
        int fl = flipped(), col = fl ? 7 - (cursor & 7) : (cursor & 7), row = fl ? cursor >> 3 : 7 - (cursor >> 3);
        if ((p & WUP_LEFT) && col > 0) col--;
        if ((p & WUP_RIGHT) && col < 7) col++;
        if ((p & WUP_UP) && row > 0) row--;
        if ((p & WUP_DOWN) && row < 7) row++;
        if (p & (WUP_LEFT | WUP_RIGHT | WUP_UP | WUP_DOWN)) { cursor = (fl ? row : 7 - row) * 8 + (fl ? 7 - col : col); dirty = 1; }
        if (p & WUP_A) { if (game_over) do_act(A_REMATCH); else select_square(cursor); }
        if (p & WUP_B) { if (sel >= 0) { sel = -1; nlm = 0; dirty = 1; } else if (game_over) do_act(A_MENU); else { dlg = D_LEAVE; dirty = 1; } }
        if (p & WUP_X) do_act(A_RESIGN);
        if (p & WUP_Y) do_act(A_DRAW);
        return;
    }
    if (nb && (p & (WUP_UP | WUP_LEFT))) { focus = (focus + nb - 1) % nb; dirty = 1; }
    if (nb && (p & (WUP_DOWN | WUP_RIGHT))) { focus = (focus + 1) % nb; dirty = 1; }
    if ((p & WUP_A) && nb && focus >= 0) do_act(btns[focus].act);
    if ((p & WUP_B) && cancel_act) do_act(cancel_act);
}

int main(void) {
    wup_screen(); lastus = wup_us(); ai_clock_ms = now_ms; ai_yield = ai_nap;
    while (wup_buttons() & (WUP_A | WUP_B)) wup_sleep(16);          /* not a press still held from the menu */
    unsigned prev = 0; uint32_t last_anim = 0;
    while (!quit_app) {
        uint32_t now = now_ms(); unsigned b = wup_buttons(), pr = b & ~prev; prev = b;
        int tx = 0, ty = 0; if (tap_poll(&tx, &ty)) on_tap(tx, ty);
        if (pr) on_buttons(pr);
        if (wifi_up && (screen == S_HOST || screen == S_JOIN)) {
            wifi_tick(&wifi);
            if (wifi_connected(&wifi)) connected();
            if ((uint32_t)(now - last_anim) > 400) { last_anim = now; dirty = 1; }
        } else if (screen == S_GAME && online) {
            rlink_poll(&rl); msg_t m; while (rlink_recv(&rl, &m)) handle_msg(m);
            int ok = conn_ok(); if (ok != conn_drawn) { conn_drawn = ok; dirty = 1; }
        }
        if (note[0] && note_until && now >= note_until) { note[0] = 0; note_until = 0; dirty = 1; }
        if (dirty) redraw();
        if (screen == S_GAME && vs_cpu && !game_over && dlg == D_NONE && pos.turn == cpu_color) {
            if (!cpu_wait) cpu_wait = now + 300;                     /* the "thinking" status is on screen first */
            else if ((int32_t)(now - cpu_wait) >= 0) { move_t cm; cpu_wait = 0; if (ai_best(&pos, level, &cm)) commit_move(cm, 1); }
        } else cpu_wait = 0;
        wup_sleep(8);                                                /* lets the firmware (input, Wi-Fi) run */
    }
    if (wifi_up) wifi_close(&wifi);
    return 0;
}
