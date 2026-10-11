/* t-look.rexx PORT [TAB n] - the test page afresh in a tab (clear, Type) */
PARSE ARG port n .
ADDRESS VALUE port
OPTIONS RESULTS
IF n ~= '' THEN 'TAB SELECT' n
'SEND "Type SYS:T/clr.txt\r"'
'WAITFOR "> " TIMEOUT 5'
'SEND "Type SYS:T/look.txt\r"'
'WAITFOR "plain" TIMEOUT 10'
SAY 'look rc' RC
EXIT 0
