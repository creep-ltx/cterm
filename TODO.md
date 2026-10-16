# CTerm - to do

Bugs and problems waiting for a fix, the most serious first. Every session working on
CTerm reads this and takes from the top; when one is fixed, delete its entry (git keeps
the history) and say how it was checked in the commit. New findings go in here as they
are found, with how to make them happen again.

How each entry was found: *verified* (made to happen), *seen* (happened once, not yet
made to happen again), *read* (from the source), *suspect* (looks wrong, not checked).

## Open bugs

### 1. Bitmap font: accented letters beyond Latin-1 and € show as ?

*Verified 2026-10-16* (FS-UAE a1200-32-mui5, CTerm 0.3b50 on its own 256-colour screen, an
Amiga bitmap font), found by the tui.library session (projects-ad). Small, not urgent.

- `Łódź Škoda €` sent as UTF-8 shows as `?ód? ?koda ?`. With a bitmap font CTerm draws
  everything beyond Latin-1 itself (is_special()), and src/draw.c fallback() has quotes,
  dashes, ticks and stars but no Latin Extended-A letters and no €.
- Fix idea: in fallback(), U+0100-017F as the letter without its accent, and a few more
  (€ E, ™ T, … ., ≈ ~, ∞ 8, arrows < > ^ v). tui.library has the same table for plain
  consoles, ready to copy: ~/Projects/CUtils/Libs/tui/src/core.c, `latinA[]` and fallback().
- To see it: `printf 'Łódź Škoda €\n'` from Linux over ssh/telnet into a CTerm tab with an Amiga
  font, or tui.library's `tuitest` (CUtils/Libs/tui/build/tuitest).

## Notes for testing

- **Driving the screen menus with wasabi:** `wasabi ui menu "View/Screen/Workbench window"` on
  CTerm's full-screen (backdrop) window is "picked" but CTerm stays on its own screen, with
  0.3b50 and 0.3b51 alike. Not looked into: CTerm, or how wasabi picks menus in a backdrop window.
- In ENV:CTerm.prefs, `mode=` is the **hex** display id, then width and height
  (`mode=00029004,640,512`); a decimal id silently gives a Workbench window (before 0.3b51
  CTerm then also saved screen=wb).
- `screen=own8` + `full=1` gives a 256-colour full screen on an AGA test machine; the
  Nord theme looks like the A1200's setup. CUtils' screenshots were made this way
  (CUtils docs/guide/README.md).
