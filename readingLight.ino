
/*
 * 
El sistema detecta los cambios de color, de esta manera no es necesaria una señal de reloj ni ninguna sincronización entre el kit y la pantalla. La señal se puede enviar a tan lento como se quiera, el único limite es que no puede ir más rápido que la velocidad de muestreo que tiene el sensor de luz. De hecho debe ir a una velocidad de alrededor de 3 veces la velocidad de muestreo del sensor, el cual para aceptar una lectura como válida debe capturarla por lo menos 2 veces.

Los colores a usar van de 0 (negro) a levelnum (blanco) el blanco se utiliza para indicar una repetición de el color anterior. La sensibilidad de el sensor disminuye mientras más rápido sea el muestreo y aumenta con un muestreo lento. Después de bastantes pruebas creo que un buen balance entre robustez y velocidad es usar una velocidad de refresh de 70ms y 9 niveles de color, lo cual permite enviar alrededor de 5 bytes/seg.

La escala de grises que se utiliza debe ser lineal para que la diferencia entre cada nivel sea la misma (o lo mas cercano posible a esto). El problema es que los monitores realizan una corrección de gama diseñada para que el ojo humano, cuya sensibilidad no es lineal (vemos mucho más detalle en los tonos obscuros). Para que nuestro sensor de luz reciba una escala lineal tenemos que revertir esta corrección:

 	math.pow(value, (1.0 / gamma))

 	donde value va de 0 (negro) 1 (blanco)

La gamma correction varia de sistema a sistema, normalmente en un rango desde 1.8 (mac) hasta 2.2 (win) para aproximarnos lo mas posible a ambos sistemas usamos 2.0
https://en.wikipedia.org/wiki/Gamma_correction

Los valores posibles se manejan asi:
	levelNum 	= número total de niveles posibles.
	REPEAT 		= el último valor en levelNum (blanco)
	MAX			= el ultimo valor aceptado (uno menos que levelNum)
	MIN 		= el primer valor aceptado (negro)

los colores posibles si usamos una escala de 8 valores son:
	0,1,2,3,4,5,6,7  --  son los valores aceptados
	8  --  es el valor de repeticion REPEAT

Para repetir un valor se utiliza el color MAX (blanco):
	0-8 	= 0 0
	0-8-0 	= 0 0 0

La secuencia de inicio INIT (en la cual se incluye una de calibrado) es:
	MAX-REPEAT-RAMP(levelNum)-MIN-REPEAT-MIN-REPEAT
	780123456780808

Si pasa DOG_TIMEOUT (en milisegundos) sin recibir nada se reinicia el proceso y se pone en espera del ciclo de INIT (y calibrado).

Despues de esto se envia STX (start text) seguido de el mensaje en ASCII en sistema octal con tres digitos para cada caracter, sólo se incluye el leading zero para completar los tres digitos para cada caracter enviado.

Para poder integrar más comandos en el futuro por esta vía se usa un caracter newLine (\n) octal - 012, para separar el commando, y los diferentes parametros. Para terminar el texto se envia ETX (end of text) octal - 003 después el CRC y al final EOT (end of transmission) octal - 004.
De esta manera queda:
	STXcomando\nparametro1\nparametro2\nparametroN\nETXCRCEOT

Falta implementar el calculo y envio del CRC entre el ETX y el EOT.

Para el comando de envio de credenciales, al que llamaremos auth:

	INITSTXauth\nmySSID\nmyPASS\nmyTOKEN\nETXCRCEOT

El CRC debe incluir  el commando + los parametros, (todo lo que se encuentre entre STX y ETX) lo cual no debe ser mayor a 1024 bytes.


FLOW
1. El usuario captura el ssid y password en un formulario al lado de una imagen que le indica donde colocar el sck en la pantalla.
2. Coloca el SCK pegado a la pantalla en la imagen y pica espacio o un mouse click o lo que sea... y el javascript hace lo siguiente:
	1. Envia la secuencia de inicio seguida de el mensaje, crc, etc
	2. Si la recepcion de el mensaje es exitosa se da feedback con el led (verde?)
	3. Enseguida se intenta la conexión al wifi y si es exitosa se sale de apmode y se da feedback con el led (azul) además de avisar a la plataforma via MQTT para que con un webSocket el usuario reciba aviso de que el sync fue exitoso.
	4. A partir de aqui la configuración e interaccion con el kit sera atraves de la plataforma via MQTT.

	
--Darle al codigo forma de libreria
--Pensar de que manera regresar los resultados (podria ser un struct)
--integrarlo en el firmware
--documentar el codigo
--hacer un documento en hackmd con todo esto
--probar que todos los caracteres posibles (UTF8??, ASCII printable??) se puedan enviar y recibir correctamente.

 */


#include <Wire.h>

#define deb SerialUSB

#define BH1730               0x29    // Direction of the light sensor

 /*    
  * El numero de valores es proporcional al brillo/contraste de la pantalla
  * 0xA0 (~3200 valores en 260 ms)
  * 0xB0 (~2100 valores en 220 ms)
  * 0xC0 (~1300 valores en 180 ms)
  * 0xD0 (~800 valores en 130 ms)
  * 0xE0 (~350 valores en 88 ms)
  * 0xF0 (~80 valores en 45 ms)
  * 0xFA (12 valores en 18 ms)
  * 0XFB (8 valores en 15 ms)
  * 
  * parece que la mejor opcion es 0xFB o FA a 70 ms --> 8 chars de 7 bits x seg. (17 valores)
  */
#define TIME0  0xFB

float newReading = 0;
float OldReading = 0;
int repetition = 0;
float tolerance = 0.30;  //podria valer la pena ajustar la tolerancia al calibrar

//asi como esta funciona perfecto con 13 niveles! (a 500 ms) ;-)

#define levelNum 9
float levels[levelNum];
int newLevel = 0;
int oldLevel = 0;

String newChar = "0";
String buffer;
String payload[8];

float localCheckSum = 0;

float watchDOG = 0;
#define DOG_TIMEOUT 2000 // If no valid char is received in this timeout we restart and calibrate again.

bool STX = false;	//start text
bool EOT = false;	//end of transmission

void setup() {
  	Wire.begin(); 
  	deb.begin(115200);
  	delay(2000);
  	ledColor(255,0,0);
  	pinMode(7, INPUT);
	feedDOG();
}

void loop() {

	if (calibrate()) {

		EOT = false;
		STX = false;

		while (!EOT) {
			char b = getChar();

			//feed the dog only with valid chars
			if (b > 0 && b < 255) feedDOG();

			// Start of text signal
			if (b == 0x02) {
				deb.println("Starting...");
				localCheckSum = 0;
				buffer = "";
				STX = true;
			}
			// End of text signal
			else if (b == 0x03) {
				STX = false;
				
				if (checksum()) {
					deb.println("Finished!!!");
					ledColor(0,255,0);
				}
			}
			// End of transmission signal
			else if (b == 0x04) {
				EOT = true;
				break;
			}

			if (STX) {

				// if char its not a newline
				if (b != 0x0A) {
					buffer.concat(b);
				} else {

					// feed the dog every newline
					// feedDOG();
					//deberia alimentar al dog cada vez que reciba un char valido
					//checar la tabla ascii para ver como implementarlo!
					// asi podria tener un timeout mucho mas corto.

					// print received line
					deb.println(buffer);
					buffer = "";
				}
			} else {
				//capture, calculate and verify CRC
			}

			
		}
	}
}

float getValue() {

	while (true) {	//CAMBIAR hay que poner un timeout o algo que impida quedarse atorado aqui...

		//obtenemos un reading del sensor
		newReading = getLight();

		//check if we need to trigger reset
		if (dogBite()) {
			if (!EOT) {
				EOT = true;
				return -1;
			}
		}

		// Si la diferencia con el valor anterior es menor a la tolerancia lo consideramos repeticion
		if (abs(newReading - OldReading) < tolerance) {
			repetition++;
	    } else {
	    	repetition = 0;
	    }

	    //esperamos a que se recupere el sensor de luz
	    delay(2);

	    // guardamos el valor viejo
	    OldReading = newReading;

	    // La lectura es valida si lleva una repeticion (ni mas ni menos)
	    if (repetition == 1) {
	    	return newReading;
	    }

	}
}

bool calibrate() {

	int newLevel = 0;
	float oldValue = 0;
	float currentValue = 0;

	while (true) {		

		//get new value
		currentValue = getValue();

		// deb.print(newLevel);

		// I value is bigger than previous we up one level
		if (currentValue > oldValue) {
			newLevel ++;
		} else {
			// If we reach the levelnum we are done!
			if (newLevel + 1 == levelNum) {
				deb.println("Calibrated!!");
				return true;
			} 

			// finished or error we start again
			newLevel = 0;
		}

		//we assign the value to this level for future readings
		levels[newLevel] = currentValue;
		// save the old value for comparison
		oldValue = currentValue;
	}

}

/* 
 *
 */
char getChar() {
	
	while(!EOT) {

		String octalString = "0";
		
		for (int i = 0; i < 3; ++i)	{
			octalString.concat(getLevel(getValue()));
		}
		
		//add the value to checksum
		int newInt = strtol(octalString.c_str(), NULL, 0);
		if (STX) localCheckSum = localCheckSum + newInt;
		
		char newChar = newInt;
		// deb.print(octalString);
		// deb.print(" ==> ");
		// deb.println(newChar);
		return newChar;
	}
}


int getLevel(float value) {
	// busca en todos los niveles
	for (int i=0; i<levelNum; i++) {
		// el primero de los niveles que este mas cerca del valor que la tolerancia
		if ( abs(levels[i] - value) < tolerance ) {

			// si el nivel es el ultimo hacemos repeticion del anterior
			if (i+1 == levelNum) {
				return oldLevel;
			}

			//save level for future comparisons
			oldLevel = i;

			//return current level
			return i;
		}
	}
}


bool dogBite() {
	if (millis() - watchDOG > DOG_TIMEOUT) {
		restart();
		return true;
	}
	return false;
} 

void feedDOG() {
	watchDOG = millis();
}


bool checksum() {
	String sum = "";

	for (int i = 0; i < 6; ++i)	{
		sum.concat(getLevel(getValue()));
	}
	
	int receivedInt = strtol(sum.c_str(), NULL, 8);

	deb.print("checksum received: ");
	deb.print(receivedInt);
	deb.print("   calculated: ");
	// We need to remove the last char (ETX-003) thats not part of the checksum
	// but is cheaper to remove it here than to filter its adition.
	deb.println(localCheckSum - 3);

	if (receivedInt == localCheckSum - 3) {
		deb.println("checksum OK");
		return true;
	} 

	deb.println("checksum ERROR!!");
	return false;
}

void restart() {
	deb.println("Restarted!! waiting for calibration signal...");
	// EOT = true;
	feedDOG();
}

/**
    Gets a raw reading from BH1730 light sensor. The resolution and speed depends on TIME0

    @return raw reading of the light sensor
*/
float getLight() {

  uint8_t GAIN0 = 0x00;     //x1
  uint8_t CONTROL = 0x07;  //continous mode only type 0 measurement 
      
  uint8_t DATA [8] = {CONTROL, TIME0, 0x00 ,0x00, 0x00, 0xFF, 0xFF ,GAIN0} ;

  float Tint = 2.8/1000.0;
  float itime_ms = Tint * 964 * (float)(256 - TIME0);
      
  uint16_t DATA0 = 0;
  uint16_t DATA1 = 0;
      
  Wire.beginTransmission(BH1730);
  Wire.write(0x80|0x00);
  for(int i= 0; i<8; i++) Wire.write(DATA[i]);
  Wire.endTransmission();
  delay(itime_ms + 1);
  Wire.beginTransmission(BH1730);
  Wire.write(0x94);
  Wire.endTransmission();
  Wire.requestFrom(BH1730, 4);
  DATA0 = Wire.read();
  DATA0=DATA0|(Wire.read()<<8);
  DATA1 = Wire.read();
  DATA1=DATA1|(Wire.read()<<8);
      
  float Lx = 0;
  float cons = 102.6 / itime_ms;
  float comp = DATA1/DATA0;

  if      (comp < 0.26) Lx = ( 1.290*(float)DATA0 - 2.733*(float)DATA1 ) / cons;
  else if (comp < 0.55) Lx = ( 0.795*(float)DATA0 - 0.859*(float)DATA1 ) / cons;
  else if (comp < 1.09) Lx = ( 0.510*(float)DATA0 - 0.345*(float)DATA1 ) / cons;
  else if (comp < 2.13) Lx = ( 0.276*(float)DATA0 - 0.130*(float)DATA1 ) / cons;
  else                  Lx = 0;

  return Lx;
}


/*------------------------------ BORRAR -----------------------------*/
//LED
#define RED     6
#define GREEN   12
#define BLUE    10

void ledColor(uint16_t red, uint16_t green, uint16_t blue){
  
  //up limit
  if (red > 255) red == 255;
  if (green > 255) green == 255;
  if (blue > 255) blue == 255;
  
  analogWrite(RED, abs(red - 255));
  analogWrite(GREEN, abs(green - 255));
  analogWrite(BLUE, abs(blue - 255));
}
