/* t-screen.rexx PORT - the screen's text */
PARSE ARG port .
ADDRESS VALUE port
OPTIONS RESULTS
'GETSCREEN'
SAY RESULT
'CURSOR'
SAY 'cursor' RESULT
EXIT 0
