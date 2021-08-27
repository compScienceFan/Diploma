#include "DHT22.h"

void setup() {
  Serial.begin(9600);
  Serial.println("Start branja\n");
}

void loop() {
  delay(MIN_MEASURMENT_INTERVAL);

  float humidity = 0, temperature = 0;

  bool readSuccessful = readDHT(humidity, temperature);
  
  if(readSuccessful){
    Serial.print("RH: ");
    Serial.print(humidity);
    Serial.print(", T: ");
    Serial.print(temperature);
    Serial.println("\n");
  }
  else{
    Serial.println("Napaka!");
  }
}
