"""mklook.py DRIVE - the links tests' files into a test Amiga's drive
(tests/links/uae.sh: `amiga-test ... dir`): SYS:T/look-l.txt (emoji, OSC 8
links, an address cut by the window's edge), the prefs (TrueType, the
fallback font, a browser= that writes the address to RAM:url.txt) and
SYS:T/CT/showurl (that "browser")."""
import os
import sys

d = sys.argv[1]
ESC = "\033"
look = (
    "Emoji: \U0001F600 \U0001F680 ☃ \U0001F431 ok ✓ 一 end\n"
    "BMP: ⌚ ⏰ ⚽ ⛄ end\n"
    "Main font past the BMP: \U0001D670\U0001D671\U0001D672 end\n"
    f"OSC 8: {ESC}]8;;http://127.0.0.1:2151/osc8{ESC}\\click here{ESC}]8;;{ESC}\\"
    f" and {ESC}]8;id=x;telnet://127.0.0.1:2152\007board{ESC}]8;;\007.\n"
    "Wrapped: " + "x" * 64 + " http://127.0.0.1:2151/a/long/path/that/goes/"
    "past/the/edge/of/the/window/x.html end\n"
)
open(os.path.join(d, "T", "look-l.txt"), "wb").write(look.encode("utf-8"))
open(os.path.join(d, "T", "CT", "showurl"), "w").write(
    ".KEY url\nEcho >RAM:url.txt \"<url>\"\n")
os.makedirs(os.path.join(d, "Prefs", "Env-Archive"), exist_ok=True)
open(os.path.join(d, "Prefs", "Env-Archive", "CTerm.prefs"), "w").write(
    "truetype=1\nttfile=SYS:Fonts/TrueType/DejaVuSansMono.ttf\nttsize=14\n"
    "fallbackfont=SYS:Fonts/TrueType/NotoEmoji-CTerm.ttf\n"
    "browser=Execute SYS:T/CT/showurl %s\n")
print("written")
