/* t-hold.rexx - started by CTerm (its menu), waits for ever: CTerm quits
 * meanwhile, the WAITFOR must end with an error and the script end
 * cleanly (written to RAM:hold.out) */
PARSE ARG port .
'WAITFOR "never-ever"'
r = RC
CALL OPEN(out, 'RAM:hold.out', 'W')
CALL WRITELN(out, 'started by CTerm, waitfor rc' r)
CALL CLOSE(out)
EXIT 0
