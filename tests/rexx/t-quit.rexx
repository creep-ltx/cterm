/* t-quit.rexx - QUIT */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
ADDRESS VALUE port
'QUIT'
SAY 'quit rc' RC
EXIT 0
