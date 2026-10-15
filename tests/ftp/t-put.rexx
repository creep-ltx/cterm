/* t-put.rexx - sends one file to the test host, in the tab last opened
 * (t-open.rexx) - or opens one when there is none: an upload stopped
 * (Esc) and sent again goes on from where it ended.
 * rx t-put.rexx file [port] */
PARSE ARG file port .
IF port = '' THEN port = 2131
ADDRESS CTERM
OPTIONS RESULTS
'TAB COUNT'
IF RESULT < 2 THEN DO
    'FTP tester:secret@127.0.0.1:' || port
    SAY 'FTP' RC
END
ELSE 'TAB SELECT' RESULT
'PUT "' || file || '"'
SAY 'PUT' RC
