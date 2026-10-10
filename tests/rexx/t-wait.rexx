/* t-wait.rexx - a WAITFOR that never ends: what it gets when CTerm quits
 * (written to RAM:wait.out) */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
'WAITFOR "never-ever"'
r = RC
CALL OPEN(out, 'RAM:wait.out', 'W')
CALL WRITELN(out, 'pending waitfor rc' r)
CALL CLOSE(out)
EXIT 0
