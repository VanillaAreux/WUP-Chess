# WUP Chess

Chess for the Wii U GamePad homebrew SDK ([libwup](../libwup)). Play the computer, a friend on one pad, or another GamePad over Wi-Fi.

## Build and run
Put this folder next to `libwup` (in the `wup-homebrew` repo root), then:

    cd wup-chess
    python3 ../libwup/tools/wupbuild.py .          # -> build/apps/wup_chess.wup
    python3 ../libwup/tools/wuplink.py build/apps/wup_chess.wup   # press Y in the Homebrew Menu first
    python3 ../libwup/tools/wuplog.py              # (optional) log output

Or copy the `.wup` to `/apps/` on the pad (FTP from Settings > Network > File Sharing).

## Playing
- **Play the computer**: pick your color and a level. Easy blunders now and then, Medium is a solid club-level
  opponent, Hard searches up to 5 s per move. The computer accepts a draw offer only when it is worse off.
- **Host online game** on one pad, **Join online game** on the other (same Wi-Fi network, joined in the Homebrew
  Menu's Settings > Network first). The host plays White; a rematch swaps colours.
- Touch a piece, then a highlighted square, or use D-pad + A. B cancels / leaves. X resigns, Y offers a draw.
- Full rules: castling, en passant, promotion (picker), check/checkmate, stalemate, 50-move rule, threefold
  repetition, insufficient material.

## Files
| File | What |
|---|---|
| `chess.c/.h` | the rules; pure C, verified against the standard perft node counts (`test_chess.c`) |
| `ai.c/.h` | the computer opponent: alpha-beta search with quiescence and piece-square evaluation; pure C (`test_ai.c`: finds mates, beats a random mover at every level) |
| `rlink.c/.h` | reliable ordered messaging (acks, retransmit, keep-alive, checksum) over any datagram transport; tested at 50% packet loss (`test_link.c`) |
| `wifi.c/.h` | the Wi-Fi transport (UDP port 28810) and LAN discovery |
| `main.c` | the touch UI and game flow |

Run the host tests with `gcc chess.c test_chess.c -o t && ./t`, `gcc chess.c ai.c test_ai.c -o t && ./t` and `gcc rlink.c test_link.c -o t && ./t`.

## Infrared
libwup has no infrared driver, so there is no IR play yet. `rlink` is transport-independent: an IR driver only
needs to provide a `transport_t` (`send` one datagram, `recv` one datagram) and the rest of the game works unchanged.
