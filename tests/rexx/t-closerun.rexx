/* t-closerun.rexx - a tab with a program Run from it: TAB CLOSE takes the
   tab at once (the program is hung up on), EndCLI leaves it till the
   program ends, QUIT ends CTerm (the window at once) */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'TAB NEW'; SAY 'new rc' RC 'tab' RESULT
'SEND "Run Wait 30\r"'
ADDRESS COMMAND 'Wait 2'
'TAB CLOSE 2'; SAY 'close rc' RC
ADDRESS COMMAND 'Wait 2'
'TAB COUNT'; SAY 'count after close (1)' RESULT
'TAB NEW'; SAY 'new rc' RC 'tab' RESULT
'SEND "Run Wait 6\r"'
ADDRESS COMMAND 'Wait 1'
'SEND "EndCLI\r"'
ADDRESS COMMAND 'Wait 2'
'TAB COUNT'; SAY 'count after endcli (2)' RESULT
ADDRESS COMMAND 'Wait 6'
'TAB COUNT'; SAY 'count after its program (1)' RESULT
'SEND "Run Wait 30\r"'
ADDRESS COMMAND 'Wait 1'
'QUIT'; SAY 'quit rc' RC
EXIT 0
