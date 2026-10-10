/* t-busy.rexx - CALL a BUSY number (fakemodem.py): the call redials for
 * a while; meanwhile another script's commands must be answered */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'CALL "Busy BBS" NOSCRIPT'
r = RC
CALL OPEN(out, 'RAM:busy.out', 'W')
CALL WRITELN(out, 'busy call rc' r 'at' TIME())
CALL CLOSE(out)
EXIT 0
