#ifndef _DHT22_H_
#define _DHT22_H_

// ************************ INCLUDES, MACROS, STATIC CONSTANTS ************************

#include <Arduino.h>

// za izpis printov v "debug mode"
#define DEBUGGING_ENABLED // ZAKOMENTIRAJ ZA "PRODUCTION MODE"

#ifdef DEBUGGING_ENABLED
  #define DEBUG_START_SERIAL() startSerial()
  #define DEBUG_PRINT(x)  Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_START_SERIAL()
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// DATA pin na katerega je vezan senzor
const static pin_size_t PIN_DHT = 0;

// stevilo prejetih bitov
const static int NUM_OF_BITS = 40;
const static int DATA_LEN = 5; // {2 bajta RH, 2 bajta TEMP, 1 bajt Checksum}

// delay-i v mikrosekundah
const static unsigned long HOST_LOW_DELAY = 1100;
const static unsigned long HOST_HIGH_DELAY = 40;
const static unsigned long RESPONSE_LOW_DELAY = 80;
const static unsigned long RESPONSE_HIGH_DELAY = 80;
const static unsigned long PRE_RECIEVE_BIT_LOW_DELAY = 50;
const static unsigned long BIT_ZERO_HIGH_DELAY = 28;
const static unsigned long BIT_ONE_HIGH_DELAY = 70;
const static unsigned long TRANSMISSION_ENDED_LOW_DELAY = 54;

const static unsigned long ERROR_ALLOWED = 20;

// milisekunde
const static unsigned long MIN_MEASURMENT_INTERVAL = 2000;

// ************************ GLOBAL VARIABLES ************************
extern unsigned long lastSensorReadTime;

// ************************ FUNCTIONS ************************

// modificirana funkcija iz https://github.com/arduino/ArduinoCore-samd/blob/master/cores/arduino/pulse.c
// za razliko od originalne zacne takoj meriti cas(cikle procesorja) ce je izbrano HIGH/LOW stanje ze v teku (in ne caka na koncanje prejsnjega pulza)
uint32_t pulseIn_modified(pin_size_t pin, uint8_t state, uint32_t timeout);
// funkcija: spremeni little endina v big endian za humidity in temperature
void swapBytes(uint8_t data [DATA_LEN]);
// funkcija: izracuna in preveri cheksum meritev iz dolzin pulza 
bool calculateMeasurements(unsigned long pulseLengths [NUM_OF_BITS], uint8_t data [DATA_LEN], int &_humidity, int &_temperature);
// funkcija: preberi temperaturo in relativno vlaznost iz senzorja (vrne true ce je bila meritev uspesna, false sicer)
bool readDHT(float &humidity, float &temperature);

#endif