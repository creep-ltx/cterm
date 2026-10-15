/* t-look.rexx - the links test page typed in the Shell tab */
ADDRESS CTERM
OPTIONS RESULTS
'SEND "Type SYS:T/look-l.txt\r"'
'WAITFOR "end" TIMEOUT 10'
SAY 'look rc' RC
EXIT 0
