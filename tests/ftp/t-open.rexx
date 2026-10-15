/* t-open.rexx - opens the test FTP host (tests/ftp/run.sh, FTPPORT 2131
 * by default) in a new tab and shows the screen: the panels' dates.
 * rx t-open.rexx [port] */
PARSE ARG port .
IF port = '' THEN port = 2131
ADDRESS CTERM
OPTIONS RESULTS
'FTP tester:secret@127.0.0.1:' || port
SAY 'FTP' RC
'GETSCREEN'
SAY RESULT
