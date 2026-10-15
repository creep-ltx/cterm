/* t-zup.rexx - the board's upload ('u'): the files dropped on the tab
 * go (no requester), the board says how lrz ended */
ADDRESS CTERM
OPTIONS RESULTS
'SEND "u"'
'WAITFOR "lrz ended" TIMEOUT 60'
SAY 'upload rc' RC
'GETSCREEN'
SAY RESULT
EXIT 0
