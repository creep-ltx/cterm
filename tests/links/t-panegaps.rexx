/* t-panegaps.rexx - ARexx PANE DWINDLE and PANE GAPS: set, read back,
 * wrong values refused; leaves two panes split with gaps 6, outer gaps
 * off, border 3 (grab to see) */
ADDRESS CTERM
OPTIONS RESULTS
'PANE DWINDLE'
SAY 'dwindle was' RESULT
'PANE DWINDLE ON'
SAY 'on rc' RC
'PANE DWINDLE'
SAY 'dwindle now' RESULT
'PANE DWINDLE MAYBE'
SAY 'maybe rc' RC '(10 wanted)'
'PANE DWINDLE OFF'
'PANE DWINDLE'
SAY 'dwindle off:' RESULT
'PANE GAPS'
SAY 'gaps were' RESULT
'PANE GAPS 6 OUTER OFF BORDER 3'
SAY 'set rc' RC
'PANE GAPS'
SAY 'gaps now' RESULT
'PANE GAPS 99'
SAY '99 rc' RC '(10 wanted)'
'PANE GAPS BORDER 9'
SAY 'border 9 rc' RC '(10 wanted)'
'PANE GAPS OUTER ON'
'PANE GAPS'
SAY 'outer on:' RESULT
'PANE GAPS OUTER OFF'
'PANE NEXT OUTER ON'
SAY 'next outer rc' RC '(10 wanted)'
'PANE SIDE'
SAY 'side rc' RC 'panes' RESULT
EXIT 0
