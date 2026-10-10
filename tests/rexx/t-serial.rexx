/* t-serial.rexx - a dial-up place through tests/serial/fakemodem.py:
 * CALL, Zmodem down, the board hangs up (a WAITFOR then ends, RC 10) */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'CALL "Modem BBS" NOSCRIPT'; SAY 'call rc' RC 'tab' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "d"'
'DOWNLOAD TIMEOUT 30'; SAY 'download rc' RC 'files' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "q"'
'WAITFOR "never-comes" TIMEOUT 20'; SAY 'after the hang-up rc' RC
EXIT 0
