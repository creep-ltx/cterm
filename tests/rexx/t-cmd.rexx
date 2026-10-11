/* t-cmd.rexx PORT command... - one command to CTerm: its RC and RESULT */
PARSE ARG port cmd
ADDRESS VALUE port
OPTIONS RESULTS
cmd
SAY 'rc' RC 'result' RESULT
EXIT 0
