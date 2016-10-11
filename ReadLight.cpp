
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

Despues de esto se envia TransmittingText (start text) seguido de el mensaje en ASCII en sistema octal con tres digitos para cada caracter, sólo se incluye el leading zero para completar los tres digitos para cada caracter enviado.

Para poder integrar más comandos en el futuro por esta vía se usa un caracter newLine (\n) octal - 012, para separar el commando, y los diferentes parametros. Para terminar el texto se envia ETX (end of text) octal - 003 después el CRC y al final EOT (end of transmission) octal - 004.
De esta manera queda:
	TransmittingTextcomando\nparametro1\nparametro2\nparametroN\nETXCRCEOT

Falta implementar el calculo y envio del CRC entre el ETX y el EOT.

Para el comando de envio de credenciales, al que llamaremos auth:

	INITTransmittingTextauth\nmySSID\nmyPASS\nmyTOKEN\nETXCRCEOT

El CRC debe incluir  el commando + los parametros, (todo lo que se encuentre entre TransmittingText y ETX) lo cual no debe ser mayor a 1024 bytes.


FLOW
1. El usuario captura el ssid y password en un formulario al lado de una imagen que le indica donde colocar el sck en la pantalla.
2. Coloca el SCK pegado a la pantalla en la imagen y pica espacio o un mouse click o lo que sea... y el javascript hace lo siguiente:
	1. Envia la secuencia de inicio seguida de el mensaje, crc, etc
	2. Si la recepcion de el mensaje es exitosa se da feedback con el led (verde?)
	3. Enseguida se intenta la conexión al wifi y si es exitosa se sale de apmode y se da feedback con el led (azul) además de avisar a la plataforma via MQTT para que con un webSocket el usuario reciba aviso de que el sync fue exitoso.
	4. A partir de aqui la configuración e interaccion con el kit sera atraves de la plataforma via MQTT.

-- Integrarlo en el firmware
-- documentar el codigo
-- hacer un documento en hackmd con todo esto
-- probar que todos los caracteres posibles (UTF8??, ASCII printable??) se puedan enviar y recibir correctamente.
-- Improve log system in the future.

 */

#include "ReadLight.h"

/*
 *  Setup must be executed before read
 */
void ReadLight::setup() {
    Wire.begin();
    feedDOG();          // Start timer for watchdog
}

/*
 *  Starts the monitoring of light sensor and process data
 */
dataLight ReadLight::read() {

  log(STARTING);

  results.ok = false;
  results.lineIndex = 0;
  memset(results.lines, 0, sizeof(results.lines));
  memset(results.log, 0, sizeof(results.log));

  if (calibrate()) {

		EOT = false;
		TransmittingText = false;

		while (!EOT) {
			char b = getChar();

			//feed the dog only with valid chars
			if (b > 0 && b < 255) feedDOG();

			// Start of text signal
			if (b == 0x02) {
        log(STX_CHAR);
				localCheckSum = 0;
				buffer = "";
				TransmittingText = true;

			} else if (b == 0x03) {
        // End of text signal
        log(ETX_CHAR);
				TransmittingText = false;
				
        // Verify checksum and finish
				if (checksum()) {
          results.ok = true;
          return results;
				} else {
          //aqui hay que retornar un error
          results.ok = false;
          return results;
        }
			}
			// End of transmission signal
			else if (b == 0x04) {
        log(EOT_CHAR);
				EOT = true;
				break;
			}

			if (TransmittingText) {

				// If received char its not a newline, we store it in buffer
				if (b != 0x0A) {
          log(NEW_LINE);
					buffer.concat(b);
				} else {

          // If we receive a new line store it in resultas and restart buffer
          results.lines[results.lineIndex] = buffer;
          results.lineIndex++;
					buffer = "";
				}
			} 
		}
	}
  results.ok = false;
  return results;
}



/*
 *  Check if we need a restart
 *  @return True if timeout has been reached
 */
bool ReadLight::dogBite() {
	if (millis() - watchDOG > DOG_TIMEOUT) {
    log(WATCHDOG_TIMEOUT);
    feedDOG();
		return true;
	}
	return false;
} 

/*
 *  Avoid the watchdog timer to trigger a restart()
 */
void ReadLight::feedDOG() {
	watchDOG = millis();
}


void ReadLight::log(debugLog message) {
  results.log[results.logIndex] = message;
  if (results.logIndex < MAX_LOG) results.logIndex++;
}


/*
 *  Gets the checksum from sender and compare it with the calculated from received text.
 *  @return True is checksum is OK, False otherwise
 */
bool ReadLight::checksum() {
	String sum = "";

  // Gets checksum digits (6) sended from the screen
	for (int i = 0; i < 6; ++i)	{
		sum.concat(getLevel(getValue()));
	}
	
	int receivedInt = strtol(sum.c_str(), NULL, 8);

	// We need to remove the last char (ETX-003) because it is not part of the checksum
	// Is cheaper to remove it here than to filter its adition.
  // Compare the calculated checksum and the received from sender
	if (receivedInt == localCheckSum - 3) {
    log(CHECKSUM_OK);
		return true;
	} 
  log(CHECKSUM_ERROR);
	return false;
}



/*
 *  Calibrates level color creating a table with valid level values from 0 to levelNum.
 *  For this process to work we need the screen to send colors from black to white with all grey levels one by one.
 *  @return True if succsesfull calibration.
 */
bool ReadLight::calibrate() {

  int newLevel = 0;
  float oldValue = 0;
  float currentValue = 0;
  feedDOG();

  while (true) {   

    // Get new value
    currentValue = getValue();

    // I value is bigger than previous go up one level
    if (currentValue > oldValue) {
      newLevel ++;
    } else {
      // If we reach the levelnum we are done!
      if (newLevel + 1 == levelNum) {
        log(CALIBRATED);
        // Keep the watchdog timer cool
        feedDOG();
        return true;
      } 

      // no calibration sequence found return false
      return false;
    }

    // We assign the value to this level for future readings
    levels[newLevel] = currentValue;

    // Save the old value for next loop comparison
    oldValue = currentValue;
  }

}



/* 
 *  Gets a char from 3 valid values obtainde from light sensor
 *  @return obtained char
 */
char ReadLight::getChar() {
  
  while(!EOT) {

    // Leading 0 indicates octal representation
    String octalString = "0";
    
    // Ask for values until we have the 3 we need for an octal ASCII char
    for (int i = 0; i < 3; ++i) {
      octalString.concat(getLevel(getValue()));
    }
    
    // Add value to checksum only if we are receiving the text portion of the payload
    int newInt = strtol(octalString.c_str(), NULL, 0);
    if (TransmittingText) localCheckSum = localCheckSum + newInt;
    
    // Convert ASCII to char
    char newChar = newInt;

    // Return the obtained char
    return newChar;
  }
  return 0x00;
}



/*
 *  Gets a valid level from a lightsensor value between the grey levels
 *  @params 
 *  @return a grey level
 */
int ReadLight::getLevel(float value) {
  // Iterate over levels testing new value
  for (int i=0; i<levelNum; i++) {
    // If level is closer than the tolerance
    if ( abs(levels[i] - value) < tolerance ) {

      // If we detect last level (REPEAT value) return the previous level
      if (i+1 == levelNum) {
        return oldLevel;
      }

      // Save level for future comparisons
      oldLevel = i;

      // Return matching level
      return i;
    }
  }
  return -1;
}



/*
 *  Checks if the value has changed and if it stays stable for at least two readings it is considered a valid value
 *  @return a validated light sensor value 
 */
float ReadLight::getValue() {

  while (true) {  //CAMBIAR hay que poner un timeout o algo que impida quedarse atorado aqui... podria ser while(!EOT)

    // Getting reading from light sensor
    newReading = getLight();

    //check if we need to restart.
    if (dogBite()) {
      if (!EOT) EOT = true;
      return levelNum + 1;
    }

    // If the new value difference from previous is less than tolerance we use it.
    if (abs(newReading - OldReading) < tolerance) {
      repetition++;
    } else {
        repetition = 0;
    }

    // Wait for light sensor to recover
    delay(2);

    // Save old reading for future comparisons
    OldReading = newReading;

    // We need ONE repetition in reading to consider it valid.
    if (repetition == 1) {
      return newReading;
    }
  }

  
}



/*
 *   Gets a raw reading from BH1730 light sensor. The resolution and speed depends on TIME0
 *   @return raw reading of the light sensor
 */
float ReadLight::getLight() {

   /*    
  * TIME0 posible values, more time = more resolution
  * 0xA0 (~3200 values in 260 ms)
  * 0xB0 (~2100 values in 220 ms)
  * 0xC0 (~1300 values in 180 ms)
  * 0xD0 (~800 values in 130 ms)
  * 0xE0 (~350 values in 88 ms)
  * 0xF0 (~80 values in 45 ms)
  * 0xFA (12 values in 18 ms)
  * 0XFB (8 values in 15 ms)
  * 
  * The best working option is 0xFB: using 9 values and printing output from screen at intervals of 70ms
  */

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