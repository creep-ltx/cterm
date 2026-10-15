/* t-tree.rexx - whole drawers by ARexx: the test host's "tree" fetched
 * into RAM:dl, fetched again (all there already), then sent back into a
 * new drawer "up" on the host (compare: diff -r tree up/tree there).
 * rx t-tree.rexx [port] */
PARSE ARG port .
IF port = '' THEN port = 2131
ADDRESS CTERM
OPTIONS RESULTS
'FTP tester:secret@127.0.0.1:' || port
SAY 'FTP' RC
ADDRESS COMMAND 'Delete RAM:dl ALL QUIET >NIL:'
ADDRESS COMMAND 'MakeDir RAM:dl'
'GET "tree" TO "RAM:dl"'
SAY 'GET tree' RC
ADDRESS COMMAND 'List RAM:dl ALL LFORMAT "%p%n %l %d %t"'
'GET "tree" TO "RAM:dl"'
SAY 'GET tree again' RC
ADDRESS COMMAND 'List RAM:dl ALL FILES LFORMAT "%p%n"'
'MKDIR "up"'
'CD "up"'
SAY 'CD up' RC RESULT
'PUT "RAM:dl/tree"'
SAY 'PUT tree' RC
'LIST'
SAY 'LIST' RESULT
'CD ".."'
'TAB CLOSE'
