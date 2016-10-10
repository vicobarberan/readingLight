
/*TODO
 * 
 * [ ] message available - detection
 * [x] continous calibration
 * [ ] design data frame
 * [ ] encoding
 * [ ] protocol
 * [ ] crc

--En cada paquete debo enviar el minimo y el maximo por lo menos una vez y resetar mis maXlevel y minLevel

--OTRA IDEA:
  + Pulir muy bien la deteccion de cambios y la tolerancia.
  + Al principio de cada paquete mando todos los valores (17)
  + Cuando detecto una escalera de 17 valores reseteo mi calibrado a ese
  + Mantengo esa calibracion hasta que termine de leer el paquete (128 pulsos?)
  + Esto resetea la escalera en cada paquete.
  De esta manera si el usuario pone el kit antes de darle play, se garantiza la escalera adecuada
  y un flujo de 128 bits seguros.

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
#define TIME0  0xFA

float newLevel = 0;
float oldLevel = 0;
float levelSize = 0;
float tolerance = 0;

#define levelNum 17

float levels[levelNum];

float maxLevel = 0;
float minLevel = 9999;

bool endDetected = false;

int buff = 0;
int oldBuff = 1;



void setup() {
  Wire.begin(); 
  deb.begin(115200);
  delay(2000);
  ledColor(0,0,255);
}

void loop() {
  readLight();
}


/**
    Sets the min, max and size values for the light range to use in the communication.
    For this function to work it needs a repetitive transition between min and max brigthness levels. 

    @return 
*/
boolean readLight() {

  int rep = 0;
  oldLevel = 0;
  
  while (!endDetected){
    newLevel = getLight();
    if (newLevel == oldLevel) {
      rep++;
    } else {
      rep = 0;
    }
    delay(2);

/*
 * El problema de no usar repetición es que las lecturas espurias 
 * (las que se obtienen de leer durante los cambios de color) ensucian mucho el calibrado.
 * Al exigir que un valor valido se presente mas de una vez limpiamos la mayoria del ruido.
 * La desventaja es que el pulso de cada color debe durar el periodo de lectura * las repeticiones
 * Aparenetemente el aumentar las repeticiones a mas de una no mejora nada, esto debe ser
 * por que las lecturas espurias nunca se presentan mas de una vez.
 * CONCLUSION: rep == 1 es la mejor opcion. (el doble de tiempo de lectura)
 */
    if (rep == 1) {

      /*
       * calibra los limites de la escala de grises 
       * El problema a resolver es como recuperar el nivel maximo 
       * una vez que detecto un valor muy alto, que esta mas lejos del valor real que la tolerancia.
       * Hay que hacer esto sin meter problemas de bajar el maximo rutinariamente.
       * 
       * Hay que forzar la aparicion de min y max en cada paquete y
       * siempre reseteamos min y max cada vez que terminemos con un paquete
       * 
       */
      if (minLevel + tolerance > newLevel) {
        minLevel = (minLevel + newLevel) / 2;
        levelSize = (maxLevel - minLevel) / (levelNum - 1);
        tolerance = levelSize * 0.49; 
//      } else if (maxLevel - tolerance < newLevel) {
      } else if (abs(maxLevel - newLevel) < tolerance || newLevel > maxLevel) {
        maxLevel = (maxLevel + newLevel) / 2;
        levelSize = (maxLevel - minLevel) / (levelNum - 1);
        tolerance = levelSize * 0.49; 
      }


      for(int i=0; i<levelNum; i++) {

        /*
         * Solo considera validos los valores cuando hay un cambio en ellos
         * De esta manera no se requiere una señal de clock ni un timming fijo
         * Lo unico que se tiene que respetar es el tiempo minimo que depende de TIME0
         * Para repetir el mismo valor usamos el ultimo bit (ej. 17) intercalandolo con el que queremos repetir
         * ej. (5 es el ultimo)
         * 11123332 = 1232
         * 15123532 = 11123332
         * Esto es si encontramos el ultimo valor asumimos otra lectura valida del caracter anterior.
         */
        if (newLevel - tolerance < levels[i] && newLevel + tolerance > levels[i]) {
          buff = i;
          if (buff != oldBuff) {  //si hay un cambio en el valor, lo considero valido
            oldBuff = buff;
            deb.println(buff+1);
          } 
        }

        //agrega valores al diccionario y calibrado permanente
        //si es igual no hace nada
        if (newLevel == levels[i]) {
          break;
        //si el valor en la table es cero lo substituye
        } else if (levels[i] == 0.0) {
          levels[i] = newLevel; 
          break;
        //si el valor es similar lo promedia
        } else if (newLevel - tolerance < levels[i] && newLevel + tolerance > levels[i]) {
          levels[i] = (levels[i] + newLevel) / 2; 
          break;
        //si el valor es menor, lo inserta y recorre los demas a la derecha
        } else if (levels[i] - tolerance > newLevel) {
          //recorre a la derecha
          for (int u=levelNum; u>i; u++ ) {
            levels[u] = levels[u-1];
          }
          levels[i] = newLevel;
          break;
        }
      }
    }
    oldLevel = newLevel;


    /*
     * Con la tecnica de insertar los mas chicos y recorrer a la derecha
     * el ultimo nivel queda mas pequeño que el maximo induciendo errores
     */
    // deb.print("min: ");
    // deb.print(minLevel);
    // deb.print(" first: ");
    // deb.print(levels[0]);
    // deb.print(" max: ");
    // deb.print(maxLevel);
    // deb.print(" last: ");
    // deb.print(levels[levelNum-1]);
    // deb.print(" size: ");
    // deb.println(levelSize);


  }
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
