/* t-zbbs.rexx - CALL the test board (tests/xfer/zbbs.py), Zmodem down
 * and up, LOG, HANGUP */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'CALL "Telnet ZBBS" NOSCRIPT'; SAY 'call rc' RC 'tab' RESULT
'LOG RAM:zbbs.log'; SAY 'log rc' RC
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "d"'
'DOWNLOAD TIMEOUT 30'; SAY 'download rc' RC 'files' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "u"'
'UPLOAD RAM:CT/Protocols/Serial RAM:CT/Protocols/XYmodem'; SAY 'upload rc' RC 'files' RESULT
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'LOG OFF'
'HANGUP'; SAY 'hangup rc' RC
'SEND "x"'; SAY 'send after hangup rc' RC
EXIT 0
