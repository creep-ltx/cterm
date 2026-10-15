/* t-zcall.rexx - a call to tests/xfer/zbbs.py on port 2153 (the links
 * rig's), in a new tab, to its menu: then drop a file on the tab and
 * start the upload with t-zup.rexx */
ADDRESS CTERM
OPTIONS RESULTS
'CALL 127.0.0.1:2153'
SAY 'call rc' RC
'WAITFOR "quit:" TIMEOUT 10'
SAY 'menu rc' RC
EXIT 0
