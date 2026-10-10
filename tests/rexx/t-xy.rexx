/* t-xy.rexx - Ymodem and Xmodem through ARexx with the test board
 * (tests/xfer/zbbs.py): DOWNLOAD and UPLOAD with PROTOCOL */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'CALL 127.0.0.1:2397'; SAY 'call rc' RC
'WAITFOR "quit:" TIMEOUT 15'; SAY 'prompt rc' RC
'SEND "y"'
'WAITFOR "receive" TIMEOUT 10'
'DOWNLOAD PROTOCOL ymodem'; SAY 'ymodem down rc' RC 'files' RESULT
'WAITFOR "quit:" TIMEOUT 15'
'SEND "x"'
'WAITFOR "receive" TIMEOUT 10'
'DOWNLOAD RAM:xm.bin PROTOCOL xmodem'; SAY 'xmodem down rc' RC 'files' RESULT
'WAITFOR "quit:" TIMEOUT 15'
'SEND "Y"'
'WAITFOR "send" TIMEOUT 10'
'UPLOAD RAM:CT/Protocols/XYmodem PROTOCOL ymodem'; SAY 'ymodem up rc' RC 'files' RESULT
'WAITFOR "quit:" TIMEOUT 15'
'SEND "q"'
EXIT 0
