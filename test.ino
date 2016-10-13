#include "ReadLight.h"

ReadLight readLight;

void setup() {

	SerialUSB.begin(115200);
	readLight.setup();
}

void loop() {

	dataLight results;

	while (!results.ok) results = readLight.read();

	for (int i=0; i<results.lineIndex; i++) {
		SerialUSB.print(results.lines[i]);
		SerialUSB.print(", ");
	}
	SerialUSB.println("");
	
}
