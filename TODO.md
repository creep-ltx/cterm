# CTerm - to do

Bugs and problems waiting for a fix, the most serious first. Every session working on
CTerm reads this and takes from the top; when one is fixed, delete its entry (git keeps
the history) and say how it was checked in the commit. New findings go in here as they
are found, with how to make them happen again.

How each entry was found: *verified* (made to happen), *seen* (happened once, not yet
made to happen again), *read* (from the source), *suspect* (looks wrong, not checked).

### 1. A failed own screen is saved as the choice: the next start is on Workbench for good

*Verified 2026-10-16* (the file changed, 2 times), cause *read*. CTerm 0.3b50, FS-UAE
a1200-32-mui5 as a 68030.

- ENVARC:CTerm.prefs said `screen=own8`, `full=1`. After CTerm opened on Workbench
  instead (the own screen did not open: once with a bad `mode=` id, once after a crash
  with a Software Failure requester still up), ENVARC:CTerm.prefs said `screen=wb`,
  `full=0`. Every later start stayed on Workbench.
- Cause (read): open_display() falls back with `scrmode = SCR_WB; fullscreen = FALSE`
  (src/screen.c:392), and the quit path saves whatever is current (src/main.c:1892,
  save_prefs()). So a one-off failure overwrites the person's saved setting.
- Fix idea: keep the wanted mode apart from the mode in use, save the wanted one, and
  say once why the own screen did not open (with the mode id).

### 2. DEADEND #81000005 in CTerm's task, once

*Seen 2026-10-16*, once, on the same rig.

- wasabid's guru report after a reset: `DEADEND ALERT #81000005 in task
  'SYS:T/CT/CTerm'`. #81000005 is exec's "memory freed twice" (AN_FreeTwice).
- It came during a run where `tar czf` was typed in a CTerm tab. That tar crashes on a 4 KB
  stack (CUtils TODO.md, entry 1) and very likely damaged memory first. So this is
  probably not CTerm's own bug: check again once tar is fixed, and delete this entry if
  it does not come back.

### 3. Ending CTerm while man shows a page resets the Amiga

*Verified 2026-10-16* (2 of 2). Filed in CUtils TODO.md (entry 2) because top and ctop
quit cleanly the same way. If man turns out innocent, CTerm's side is the tab's end
while a program reads raw keys with WAIT_CHAR.

### 4. wget's progress line repeats in a Workbench window

*Verified 2026-10-16.* Filed in CUtils TODO.md (entry 4): in a window started with
`COLS 80 ROWS 20` each `\r` redraw of a line exactly as wide as the window came out on a
new line; on CTerm's own full screen it redraws in place. If wget's width is right, the
fault is CTerm's wrap at the last column (a VT100 holds the wrap until the next character).

## Notes for testing

- In ENV:CTerm.prefs, `mode=` is the **hex** display id, then width and height
  (`mode=00029004,640,512`); a decimal id silently gives Workbench (entry 1 then saves it).
- `screen=own8` + `full=1` gives a 256-colour full screen on an AGA test machine; the
  Nord theme looks like the A1200's setup. CUtils' screenshots were made this way
  (CUtils docs/guide/README.md).

### Bitmap font: accented letters beyond Latin-1 and € show as ?

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
