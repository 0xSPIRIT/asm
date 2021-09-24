SET REG[29] 1 // Set output mode to integer.

STR 0 "Input a number: "
OSR 0
GET REG[2]

SET REG[1] REG[30]
SET REG[29] 0 // Set output mode to string.
SET MEM[REG[1]] REG[2]
INC REG[1]
SET MEM[REG[1]] 0
OSR 0