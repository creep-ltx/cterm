/* t-ftps.rexx - FTPS against the test host (FTPPORT=2132 tests/ftp/run.sh
 * start --tls PEM): the place opened by its URL (a certificate not met
 * before is asked about in the tab: answer y there), then LIST, a file
 * fetched, one sent, a drawer fetched. rx t-ftps.rexx [port] */
PARSE ARG port .
IF port = '' THEN port = 2132
ADDRESS CTERM
OPTIONS RESULTS
'FTP ftps://tester:secret@127.0.0.1:' || port
SAY 'FTPS' RC
'LIST'
SAY 'LIST' RC RESULT
ADDRESS COMMAND 'Delete RAM:tls ALL QUIET >NIL:'
ADDRESS COMMAND 'MakeDir RAM:tls'
'GET "big.bin" "summer.txt" TO "RAM:tls"'
SAY 'GET' RC
'GET "tree" TO "RAM:tls"'
SAY 'GET tree' RC
'PUT "RAM:tls/summer.txt"'
SAY 'PUT' RC
'MKDIR "sent"'
'CD "sent"'
'PUT "RAM:tls/tree"'
SAY 'PUT tree' RC
'CD ".."'
ADDRESS COMMAND 'List RAM:tls ALL LFORMAT "%p%n %l %d %t"'
