/* t-tabs.rexx - TAB NEW, COUNT, NAME, SELECT, CLOSE */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'TAB COUNT'; SAY 'count' RESULT
'TAB NEW'; SAY 'new rc' RC 'tab' RESULT
'TAB COUNT'; SAY 'count' RESULT
'TAB CURRENT'; SAY 'current' RESULT
'SEND "echo in-tab-two\r"'
'WAITFOR "in-tab-two" TIMEOUT 5'; SAY 'waitfor in tab 2 rc' RC
'TAB NAME 2'; SAY 'name 2' RESULT
'TAB SELECT 1'; SAY 'select rc' RC 'result' RESULT
'TAB CURRENT'; SAY 'current' RESULT
'TAB CLOSE 2'; SAY 'close rc' RC
ADDRESS COMMAND 'Wait 1'
'TAB COUNT'; SAY 'count' RESULT
'TAB SELECT 7'; SAY 'select 7 rc' RC
'WINDOW ACTIVATE'; SAY 'window rc' RC
'SCREEN TOFRONT'; SAY 'screen rc' RC
'WINDOW HIDE'; SAY 'hide rc' RC
EXIT 0
