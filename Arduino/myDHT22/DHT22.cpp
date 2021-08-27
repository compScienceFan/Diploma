#include "DHT22.h"

unsigned long lastSensorReadTime = 0;

// 1) ZACETEK KOMUNIKACIJE GOSTITELJA (pinMode = OUTPUT):
//    nastavi DATA pin na LOW za vsaj 1000 mikros (nato drzi HIGH za 20-40 mikros)
//
// 2) RESPONSE NAPRAVE (pinMode = INPUT_PULLUP, lahko bi malo delayal da se stanje vzpostavi, ampak ni nuja):
//    dht poslje response, da je pripravljen -> cca 80 mikros LOW, temu sledi se 80 mikros HIGH
//
// (2b) PRIPRAVA NA POSILJANJE MERITEV (40 bitov -> 5 segmentov po 8 bitov)
//    1 -> celi del RH, 2 -> decimalni del RH, 3 -> celi del T, 4 -> decimalni del T, 5 -> checksum (more bit enak binarni vsoti prvih stirih bajtov)
//
// 3) SPREJEMANJE BITOV:
// 3a) bit 0 -> LOW 50 mikros in HIGH 26-28 mikros
// 3b) bit 1 -> LOW 50 mikros in HIGH 70 mikros
// 3c) vrednosti za tempereaturo in vlaznost se pretvorijo v decimalno (vsaka ima dva bajta) in delijo z 10
// 3d) ! NASTAVLJEN MSB PRI TEMPERATURI DOLOČA, DA JE NEGATIVNA
//
// * ni obvezno 4) KONEC SPREJEMANJA BITOV / KOMUNIKACIJE
//    LOW 54 mikros in potem HIGH...
//
//
// !!! ena meritev na 2 sekundi !!!
//
//

// modificirana funkcija iz https://github.com/arduino/ArduinoCore-samd/blob/master/cores/arduino/pulse.c
// za razliko od originalne zacne takoj meriti cas(cikle procesorja) ce je izbrano HIGH/LOW stanje ze v teku (in ne caka na koncanje prejsnjega pulza)
uint32_t pulseIn_modified(pin_size_t pin, uint8_t state, uint32_t timeout)
{
  // cache the port and bit of the pin in order to speed up the
  // pulse width measuring loop and achieve finer resolution.  calling
  // digitalRead() instead yields much coarser resolution.
  PinDescription p = g_APinDescription[pin];
  uint32_t bit = 1 << p.ulPin;
  uint32_t stateMask = state ? bit : 0;

  // convert the timeout from microseconds to a number of times through
  // the initial loop; it takes (roughly) 13 clock cycles per iteration.
  uint32_t maxloops = microsecondsToClockCycles(timeout) / 13;

  const volatile uint32_t *port = &(PORT->Group[p.ulPort].IN.reg);
  uint32_t width = 0;

  // wait for the pulse to start
  while ((*port & bit) != stateMask)
    if (--maxloops == 0)
      return 0;
  // wait for the pulse to stop
  while ((*port & bit) == stateMask) {
    if (++width == maxloops)
      return 0;
  }

  // convert the reading to microseconds. The loop has been determined
  // to be 13 clock cycles long and have about 16 clocks between the edge
  // and the start of the loop. There will be some error introduced by
  // the interrupt handlers.
  if (width)
    return clockCyclesToMicroseconds(width * 13 + 16);
  else
    return 0;
}

// funkcija: spremeni little endina v big endian za humidity in temperature
void swapBytes(uint8_t data [DATA_LEN]){
  uint8_t temp = data[0];
  data[0] = data[1];
  data[1] = temp;

  temp = data[2];
  data[2] = data[3];
  data[3] = temp;
}

// funkcija: izracuna in preveri cheksum meritev iz dolzin pulza 
bool calculateMeasurements(unsigned long pulseLengths [NUM_OF_BITS], uint8_t data [DATA_LEN], int &_humidity, int &_temperature){
  data[0] = data[1] = data[2] = data[3] = data[4] = 0;

  // sestavi vseh 40 meritev dolzin pulzov v 5 bajtov
  for(int i = 0; i < NUM_OF_BITS; i++){
    data[i / 8] <<= 1;
    if(pulseLengths[i] == 0){
      // timeout pri merjenju dolzine pulza za dekodiranje v bit, prislo je do napake
      DEBUG_PRINTLN("timeout pri merjenju dolzine pulsa za dekodiranje v bit");
      return false;
    }
    else if(pulseLengths[i] >= (BIT_ZERO_HIGH_DELAY + BIT_ONE_HIGH_DELAY) / 2){
      // bit je 1 (drugace je 0)
      data[i / 8] |= 0x1;
    }
  }

  DEBUG_PRINT(data[0]);
  DEBUG_PRINT(", ");
  DEBUG_PRINT(data[1]);
  DEBUG_PRINT(", ");
  DEBUG_PRINT(data[2]);
  DEBUG_PRINT(", ");
  DEBUG_PRINT(data[3]);
  DEBUG_PRINT(" <=> ");
  DEBUG_PRINTLN(data[4]);

  // zamenjaj bajta pri temperaturi in vlažnosti
  swapBytes(data);

  uint8_t sumOfMeasurments = (data[0] + data[1] + data[2] + data[3]) & 0xFF; // & 0xFF -> AND operacija, obdrzi samo spodnjih 8 bitov vsote (datasheet)
  // primerjaj vsoto z checksum (datasheet)
  if(sumOfMeasurments == data[4]){ 
    uint16_t* ptr = (uint16_t*)data;
    _humidity = (int)*ptr;
    _temperature = (int)*(ptr + 1);
    
    // v primeru da je pri temperaturi MSB nastavljen, potem bo negativna, pozitivna sicer (datasheet)
    if(_temperature >> 15 == 0x1){
      _temperature &= ~(0x1 << 15);
      _temperature *= -1;
    }

    return true;
  }

  else{
    // checksum se ne ujema s sestevki meritev
    DEBUG_PRINTLN("checksum se ne ujema s sestevki meritev");
    return false;
  }
}

// funkcija: preberi temperaturo in relativno vlaznost iz senzorja (vrne true ce je bila meritev uspesna, false sicer)
bool readDHT(float &humidity, float &temperature){
  unsigned long currTime = millis();
  if(currTime - lastSensorReadTime < MIN_MEASURMENT_INTERVAL){
    // vrni false, cas med meritvama more biti vsaj MIN_MEASURMENT_INTERVAL
    DEBUG_PRINTLN("cas med meritvama more biti vsaj " + String(MIN_MEASURMENT_INTERVAL));
    return false;
  }
  
  lastSensorReadTime = currTime;
  uint8_t data [DATA_LEN]; // {2 bajta RH, 2 bajta TEMP, 1 bajt Checksum}
  unsigned long pulseLengths [NUM_OF_BITS];

  // 1) ZACETEK KOMUNIKACIJE GOSTITELJA (sprozi LOW nato HIGH)
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delayMicroseconds(HOST_LOW_DELAY);
  digitalWrite(PIN_DHT, HIGH);

  // 2) RESPONSE NAPRAVE (prestavi na input_pullup - ko naprava miruje je napetost visoka, preberi LOW nato HIGH)
  pinMode(PIN_DHT, INPUT_PULLUP);

  unsigned long pulseLengthLow = pulseIn_modified(PIN_DHT, LOW, RESPONSE_LOW_DELAY + ERROR_ALLOWED);
  if(pulseLengthLow == 0 || pulseLengthLow < RESPONSE_LOW_DELAY - ERROR_ALLOWED){
    // vrni false, ker se cas na pinu za LOW ne ujema s casom iz datasheeta
    DEBUG_PRINT(pulseLengthLow);
    DEBUG_PRINTLN("response LOW ni ustrezen");
    return false;
  }

  unsigned long pulseLengthHigh = pulseIn_modified(PIN_DHT, HIGH, RESPONSE_HIGH_DELAY + ERROR_ALLOWED);
  if(pulseLengthHigh == 0 || pulseLengthHigh < RESPONSE_HIGH_DELAY - ERROR_ALLOWED){
    // vrni false, ker se cas na pinu za HIGH ne ujema s casom iz datasheeta
    DEBUG_PRINTLN("response HIGH ni ustrezen");
    return false;
  }

  // 3) SPREJEMANJE BITOV (sprejmi 40 bitov, shrani si trajanje HIGH stanja pri vsakem posebej)
  for(int i = 0; i < NUM_OF_BITS; i++){
    pulseIn_modified(PIN_DHT, LOW, BIT_ONE_HIGH_DELAY + ERROR_ALLOWED);
    pulseLengths[i] = pulseIn_modified(PIN_DHT, HIGH, BIT_ONE_HIGH_DELAY + ERROR_ALLOWED);
  }

  /* ---lahko preskocis---
  //4) KONEC SPREJEMANJA BITOV / KOMUNIKACIJE (preveri ce se poslje ustrezno dolg LOW signal, nato zacni z obdelavo bitov)
  pulseLengthLow = pulseIn_modified(PIN_DHT, LOW, TRANSMISSION_ENDED_LOW_DELAY + ERROR_ALLOWED);

  if(pulseLengthLow == 0 || pulseLengthLow < TRANSMISSION_ENDED_LOW_DELAY - ERROR_ALLOWED){
    // vrni false, ker se cas na pinu za LOW ne ujema s casom iz datasheeta
    DEBUG_PRINTLN("konec sprejemanja LOW ni ustrezen " + String(pulseLengthLow));
    return false;
  }*/
  
  // vse OK, nadaljna obdelava prejetih bitov
  int _humidity = 0, _temperature = 0;
  if(calculateMeasurements(pulseLengths, data, _humidity, _temperature)){
    // meritve se na koncu se mnozijo z 0.1 (datasheet)
    humidity = _humidity * 0.1;
    temperature = _temperature * 0.1;
    return true;
  }
  else{
    return false;
  }
}