/* t-menu.rexx - started from CTerm's menu (Project/Run ARexx script...):
 * the port's name comes as the argument, ADDRESS is set already. */
PARSE ARG port .
OPTIONS RESULTS
'VERSION'; v = RESULT
CALL OPEN(out, 'RAM:menu.out', 'W')
CALL WRITELN(out, 'arg' port 'address' ADDRESS() 'version' v)
CALL CLOSE(out)
EXIT 0
