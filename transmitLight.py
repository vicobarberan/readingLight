import pygame
import binascii
import time
import serial
import schedule
import sys
import math

texto = 'hola'
credentials = oct(int(binascii.hexlify(texto), 16))[1:]

#esto lo covierte en hexadecimal
# res = binascii.hexlify(texto)

#esto lo hace decimal
#int(res, 16)

#esto lo hace octal
#oct()

# para regresarla:
binascii.unhexlify('%x' % int(credentials,8))

# payload = '00000010' + credentials + '00000100'

withSerial = False

gamma = 2.0
levelNum = 9
MIN = 0
MAX = levelNum - 2
REPEAT = levelNum - 1

previousNum = 10

#El checksum sera de 6 digitos en octal (777777) permite 32kbytes
checksum = 0

# el periodo no puede ser menor que el refresh rate de la pantalla. (60hz - 16.6ms)
# Creo que lo mas seguro seria mantenernos abajo de 30hz (33.3ms) para estar seguros de que funcione en cualquier hardware.
# periodo = .070
periodo = .070

if withSerial: 
	SCK = serial.Serial('/dev/ttyACM0', 115200)
	SCK.timeout = periodo


'''
Low level function to color screen to a determined value
This function takes care of the color repetition, so it should be the last to manage values
'''
def outDigit(digit):
	# global variable to know wath was sended before and implement repetitions
	global previousNum

	#makes sure that digit is a char
	digit = str(digit)

	# if this value was the last sended use the REPEAT value 
	if digit == previousNum:
		# print "REPEAT"
		previousNum = 10
		paint(REPEAT)
	# if the previous sended value is diferent from this, send it as is
	else:
		paint(int(digit))
		previousNum = digit

'''
Sends a single character
'''
def sendChar(char):
	global checksum
	checksum = checksum + int(oct(int(binascii.hexlify(char), 16)),8)

	# converts char to his ASCII octal representation
	asciiChar = oct(int(binascii.hexlify(char), 16))[1:]
	
	# if the value has less than 3 digits fill it with leading zeros
	while len(asciiChar) < 3:
		asciiChar = "0" + str(asciiChar)

	# sends the digits
	for digit in asciiChar:
		outDigit(digit)

	print char + " ==> " + asciiChar

'''
outputs a ramp of colors from 0 to valores
'''
def ramp(valores):
	for i in range(valores):
		paint(i)

'''
Sends a word char by char
'''
def sendWord(word):
	for letter in word:
		sendChar(letter)


def sendChecksum():
	global checksum
	toSend = oct(checksum)
	while(len(toSend) < 6):
		toSend = '0' + toSend

	for digit in toSend:
		outDigit(digit)

	print "checksum: " + toSend


'''
Init sequence, after this we can start sending the data
'''
def INIT():
	paint(MAX)
	paint(REPEAT)
	ramp(levelNum)
	paint(MIN)
	paint(REPEAT)
	paint(MIN)
	paint(REPEAT)

	print("INIT")

	time.sleep(periodo*5)

'''
Sends end of text char ETX
'''
def ETX():
	outDigit(0)
	outDigit(0)
	outDigit(3)
	print "ETX ==> 003"

'''
Sends end of transmission char EOT
'''
def EOT():
	outDigit(0)
	outDigit(0)
	outDigit(4)
	print "EOT ==> 004"


'''
Sends new line char LF
'''
def newLine():
	outDigit(0)
	outDigit(1)
	outDigit(2)
	print "LF ==> 012"

'''
Sends start of text char STX
'''
def STX():
	outDigit(0)
	outDigit(0)
	outDigit(2)
	print "STX ==> 002"


'''
Inverts gamma correction improving linearity of values

@param value requested value (0-levelNum)
@param levelNum maximum value of the used scale
'''
def getColor(value, levelNum):
	previo = (value * (255.0/(levelNum - 1)))
	final = 255.0 * math.pow((previo / 255.0), (1.0 / gamma))
	return (final,final,final)

'''
Low level function that changes window color
'''
def paint(colorValue):
	# fills window with requested value
	screen.fill(getColor(colorValue, levelNum))
	
	if withSerial:
		returned = SCK.readline()
		rr = str(returned).strip()
		# print "returned: " + rr
	
	#pygame stuff
	pygame.display.flip()

	# Sleeps for the requested period.
	time.sleep(periodo)


(width, height) = (300, 300)
screen = pygame.display.set_mode((width, height))

print str(periodo*1000) + " ms -- " + str( ( (1/periodo) * math.sqrt(levelNum-1) ) / 8.0 ) + " bytes/seg" 

state = 1

while True:

	# There should be a pause of more time than the watchdog timeout to give the kit time to restart after errors.
	paint(MIN)
	time.sleep(1.5)

	checksum = 0
	INIT()

	STX()
	sendWord("auth\n")
	sendWord("mySSID\n")
	sendWord("myPASS\n")
	sendWord("myTOKEN\n")
	ETX()

	sendChecksum()
	EOT()


	# if state == 1: paint(1)
	# elif state == 2: 
	# 	for i in range(2):
	# 		ramp(levelNum)

	# 	state = state + 1
	# elif state == 3:

	events = pygame.event.get()

	for event in events:
		if event.type == pygame.KEYDOWN:
			if event.key == 27: sys.exit() # esc key exits
			if event.key == 32: state = 2	# space starts calibration
			
			if event.key == 275:
					# faster
					periodo = periodo - 0.005
					print str(periodo*1000) + " ms -- " + str( ( (1/periodo) * math.sqrt(levelNum-1) ) / 8.0 ) + " bytes/seg"
			if event.key == 276:
					# slower
					periodo = periodo + 0.005
					print str(periodo*1000) + " ms -- " + str( ( (1/periodo) * math.sqrt(levelNum-1) ) / 8.0 ) + " bytes/seg"

			print event.key

			if state == 3: sendChar(str(unichr(event.key)))

			

'''
2	00000010	2h		start of text
3	00000011	3h		end of text
4	00000100	4h		end of transmission
'''

