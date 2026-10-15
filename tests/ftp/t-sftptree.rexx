/* t-sftptree.rexx - whole drawers over SFTP (run in CTermSSH: its port
 * CTERM; the browser's tab is opened by the script): the host's drawer
 * "tree" fetched into RAM:sdl, then sent back into "up" there.
 * rx t-sftptree.rexx creep@127.0.0.1:2133/tmp/sftp-ctftp */
PARSE ARG place .
ADDRESS CTERM
OPTIONS RESULTS
'SFTP' place
SAY 'SFTP' RC
'LIST'
SAY 'LIST' RC RESULT
ADDRESS COMMAND 'Delete RAM:sdl ALL QUIET >NIL:'
ADDRESS COMMAND 'MakeDir RAM:sdl'
'GET "tree" TO "RAM:sdl"'
SAY 'GET tree' RC
ADDRESS COMMAND 'List RAM:sdl ALL LFORMAT "%p%n %l %d %t"'
'MKDIR "up"'
'CD "up"'
'PUT "RAM:sdl/tree"'
SAY 'PUT tree' RC
'CD ".."'
