SET REG[0] 65
SET REG[1] 126
JSR main

FUN main:
	OUT REG[0]
	INC REG[0]
	CMP REG[1] REG[0]
	SKI
	JSR main

SET MEM[0] 10
STR 1 "The program is finished!"
OSR 0
