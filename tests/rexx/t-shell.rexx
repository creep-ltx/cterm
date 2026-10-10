/* t-shell.rexx - SEND to the local Shell, WAITFOR its answer, the screen */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
OPTIONS RESULTS
'SEND "Eval 6*7\r"'
SAY 'send rc' RC
t0 = TIME('E')
'WAITFOR "42" TIMEOUT 10'
SAY 'waitfor rc' RC 'result' RESULT
'GETSCREEN'
s = RESULT
SAY 'screen has 42:' (POS('42', s) > 0) 'lines:' (LENGTH(s) - LENGTH(COMPRESS(s, '0a'x)) + 1)
'CURSOR'; SAY 'cursor' RESULT
t0 = TIME('R')
'WAITFOR "never-comes" TIMEOUT 2'
SAY 'timeout rc' RC 'after' TRUNC(TIME('E'),1) 's'
'WAITFOR "zq1" "zq2" TIMEOUT 0'
SAY 'timeout 0 rc' RC
'NOSUCH'
SAY 'unknown rc' RC
'GETLINE 999'
SAY 'bad row rc' RC
EXIT 0
