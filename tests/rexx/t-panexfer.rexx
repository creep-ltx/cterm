/* t-panexfer.rexx PORT - call the test board, Zmodem down (big then small) */
PARSE ARG port .
ADDRESS VALUE port
OPTIONS RESULTS
'CALL 127.0.0.1:2397'; SAY 'call rc' RC 'tab' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "d"'
t = TIME('R')
'DOWNLOAD TIMEOUT 600'; SAY 'download rc' RC 'files' RESULT 'in' TRUNC(TIME('E'),1) 's'
EXIT 0
