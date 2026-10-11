/* call a place */
PARSE ARG place
ADDRESS CTERM
OPTIONS RESULTS
'CALL "' || place || '"'
SAY RC RESULT
