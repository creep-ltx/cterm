/* info.rexx - what a running CTerm says about itself.
 *   rx info.rexx [port]       (port: CTERM, the first CTerm, if none)
 */
PARSE ARG port .
IF port = '' THEN port = 'CTERM'
IF ~SHOW('P', port) THEN DO
    SAY 'No' port 'port: is CTerm running?'
    EXIT 10
END
ADDRESS VALUE port
OPTIONS RESULTS

'VERSION';        SAY 'Version: ' RESULT
'TAB COUNT';      SAY 'Tabs:    ' RESULT
'TAB CURRENT';    n = RESULT
'TAB NAME' n;     SAY 'This tab:' n '-' RESULT
'CURSOR';         SAY 'Cursor:  ' RESULT '(row column)'
'GETLINE';        SAY 'Its line:' RESULT
'PHONEBOOK';      SAY 'Places:  ' RESULT
'HELP';           SAY 'Commands:' RESULT
EXIT 0
