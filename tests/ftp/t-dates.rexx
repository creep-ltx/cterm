/* t-dates.rexx - the test FTP host's dates in the browser's ARexx LIST
 * and on screen, then two files fetched and their dates on the Amiga
 * (List shows them). rx t-dates.rexx [port] [listing] - listing: MLSD
 * (default) or LIST, only said in the output. */
PARSE ARG port how .
IF port = '' THEN port = 2131
ADDRESS CTERM
OPTIONS RESULTS
'FTP tester:secret@127.0.0.1:' || port
SAY 'FTP' how RC
'GETSCREEN'
scr = RESULT
DO WHILE scr ~= ''
    PARSE VAR scr l '0a'x scr
    IF POS('.txt', l) > 0 THEN SAY STRIP(l, 'T')
END
ADDRESS COMMAND 'Delete RAM:dates ALL QUIET >NIL:'
ADDRESS COMMAND 'MakeDir RAM:dates'
'GET "summer.txt" "winter.txt" TO "RAM:dates"'
SAY 'GET' RC RESULT
ADDRESS COMMAND 'List RAM:dates LFORMAT "%n %d %t"'
'TAB CLOSE'
