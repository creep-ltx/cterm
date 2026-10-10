/* bbs-login.rexx - logs in to a BBS by itself: put it in a phone book
 * place's Script field, and CTerm runs it when the call is up (with its
 * port's name as the argument and ADDRESS already set to CTerm).
 *
 * Change the words below to what your board asks, and your name. Every
 * WAITFOR has a time limit: RC 5 means the words never came.
 */
PARSE ARG port .
IF port ~= '' THEN ADDRESS VALUE port
OPTIONS RESULTS

name = 'Your Name'          /* or leave it to the phone book's user name */
password = ''               /* empty: you type it yourself */

'WAITFOR "name" "login" TIMEOUT 30'
IF RC ~= 0 THEN EXIT 5
'SEND "'name'\r"'

IF password ~= '' THEN DO
    'WAITFOR "password" TIMEOUT 20'
    IF RC ~= 0 THEN EXIT 5
    'SEND "'password'\r"'
END

/* past the welcome screens: a key at each "press" */
DO 5
    'WAITFOR "press" "main menu" TIMEOUT 15'
    IF RC ~= 0 THEN LEAVE
    IF INDEX(UPPER(RESULT), 'PRESS') = 0 THEN LEAVE
    'SEND "\r"'
END
EXIT 0
