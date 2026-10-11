/* t-flood.rexx PORT [cmd] - a flood in the Shell tab (list SYS: ALL), its end waited for */
PARSE ARG port cmd
IF cmd = '' THEN cmd = 'list SYS: ALL'
ADDRESS VALUE port
OPTIONS RESULTS
t = TIME('R')
'SEND "' || cmd || '\r"'
'SEND "Eval 1 LFORMAT QZX%NQZX\r"'
'WAITFOR "QZX1QZX" TIMEOUT 900'
SAY 'flood rc' RC 'after' TRUNC(TIME('E'),1) 's'
'WINDOW STATE'
SAY 'state' RESULT
EXIT 0
