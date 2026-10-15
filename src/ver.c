/* ver - CTerm's version string, on its own and linked first (not through
 * -flto, which places things where it likes): it then sits at the start of
 * the code. In the data it once fell across 128 KB of the file and
 * C:Version, reading the file in pieces, showed "CTm 0.3b33". */
const char version[] __attribute__((section(".text"))) = "$VER: CTerm 0.3b47 (15.10.2026)";
