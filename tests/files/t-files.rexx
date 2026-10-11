/* t-files.rexx - the file browser's ARexx commands on the place in
 * tab 1 (tests/files): LIST, MKDIR, CD, PUT, RENAME, GET, DELETE.
 * Needs SYS:T/up é.txt (Latin-1 name) on the Amiga. */
ADDRESS CTERM
OPTIONS RESULTS
TAB SELECT 1
'LIST'; SAY 'LIST' RC RESULT
'MKDIR "new dir"'; SAY 'MKDIR' RC
'CD "new dir"'; SAY 'CD' RC RESULT
'PUT "SYS:T/up é.txt"'; SAY 'PUT' RC RESULT
'RENAME "up é.txt" "renamed é.txt"'; SAY 'RENAME' RC
'LIST'; SAY 'LIST' RC RESULT
'GET "renamed é.txt" TO "RAM:"'; SAY 'GET' RC RESULT
'DELETE "renamed é.txt"'; SAY 'DELETE' RC
'CD ".."'; SAY 'CD' RC RESULT
'DELETE "new dir"'; SAY 'DELETE drawer' RC
'GET "small.txt" "big.bin" TO "RAM:"'; SAY 'GET two' RC RESULT
'GET "no such file"'; SAY 'GET missing' RC
'LIST'; SAY 'LIST' RC RESULT
