/* zlogin.rexx - a phone book place's login script for the test board:
 * CTerm runs it with its port's name when the call is up. Writes what
 * it saw to RAM:zlogin.out. */
PARSE ARG port .
OPTIONS RESULTS
CALL OPEN(out, 'RAM:zlogin.out', 'W')
CALL WRITELN(out, 'port' port 'address' ADDRESS())
'WAITFOR "quit:" TIMEOUT 15'
CALL WRITELN(out, 'prompt rc' RC 'result' RESULT)
'TAB CURRENT'
CALL WRITELN(out, 'tab' RESULT)
'SEND "q"'
CALL WRITELN(out, 'send rc' RC)
CALL CLOSE(out)
EXIT 0
