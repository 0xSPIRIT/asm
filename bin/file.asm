STR 0 "Input a number: "
OSR 0

GET REG[0]

STR 0 "You just typed "
SET REG[1] REG[30]
DEC REG[1]
DEC REG[1]
SET REG[1] REG[0]
OSR 0
