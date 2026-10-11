/* t-hidexfer.rexx PORT - a Zmodem download, the window hidden while it runs */
PARSE ARG port .
ADDRESS VALUE port
OPTIONS RESULTS
'CALL 127.0.0.1:2397'; SAY 'call rc' RC 'tab' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "d"'
'WAITFOR "sending" TIMEOUT 10'

'WINDOW HIDE'; SAY 'hide rc' RC
t = TIME('R')
'DOWNLOAD TIMEOUT 600'; SAY 'download rc' RC 'files' RESULT 'in' TRUNC(TIME('E'),1) 's'
'WINDOW STATE'; SAY 'state' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
EXIT 0
