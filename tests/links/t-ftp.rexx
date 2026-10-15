/* t-ftp.rexx [LIST] - the file browser to tests/ftp/ftpd.py on port
 * 2154 (anonymous), in a new tab; LIST: what is in its drawer now (after
 * a file was dropped on the tab) */
PARSE ARG what .
ADDRESS CTERM
OPTIONS RESULTS
IF what = 'LIST' THEN DO
    'LIST'
    SAY 'list rc' RC ':' RESULT
    EXIT 0
END
'FTP 127.0.0.1:2154'
SAY 'ftp rc' RC
EXIT 0
