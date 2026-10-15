/* t-get.rexx - one file fetched from the test host into RAM:, in the tab
 * last opened (or a new one: the URL given), the time it took said.
 * rx t-get.rexx name [url] - url e.g. ftps://tester:secret@127.0.0.1:2132 */
PARSE ARG name url .
ADDRESS CTERM
OPTIONS RESULTS
'TAB COUNT'
IF RESULT < 2 THEN DO
    IF url = '' THEN url = 'tester:secret@127.0.0.1:2131'
    'FTP' url
    SAY 'FTP' RC
END
ELSE 'TAB SELECT' RESULT
ADDRESS COMMAND 'Delete RAM:' || name || ' QUIET >NIL:'
t = TIME('E')
'GET "' || name || '" TO "RAM:"'
SAY 'GET' RC 'in' TIME('E') - t 's'
