/* t-send.rexx PORT text - SEND the text (\r etc. as SEND takes them) */
PARSE ARG port text
ADDRESS VALUE port
OPTIONS RESULTS
'SEND "' || text || '"'
SAY 'send rc' RC
EXIT 0
