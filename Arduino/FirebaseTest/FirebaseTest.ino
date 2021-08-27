// vnaprej pripravljene knjiznice
#include "Firebase_Arduino_WiFiNINA.h"
#include <WiFiNINA.h>
#include <RTCZero.h>

// wifi, firebase avtorizacija (skrito)
#include "mySecrets.h"

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

// maximalno stevilo poskusov povezave na wifi ob neuspehu (potem preneha)
#define MAX_RETRIES_CONNECT_WIFI 5

// pin, kjer se nahaja onboard led
#define LED_PIN 6

// objekt firebase
FirebaseData firebaseData;
// RTC objekt
RTCZero rtc;

// GMT time zone + poletni cas
const int GMT = 2;
const int YEAR_OFFSET = 2000;
// spremenljivke
uint8_t nextHours = 0;
uint8_t nextMinutes = 0;
uint8_t nextSeconds = 0;
uint8_t offsetGMT = 2; // +1 + poletni cas

// belezenje neuspehov povezave (sekunde epohe)
long errorDates [100];
bool errorTypes [100]; // true za wifi, false za firebase
int errorIndex = -1;

// funkcija: zacni Serial
void startSerial(){
  Serial.begin(9600);
  delay(2000);
  DEBUG_PRINTLN();
}

// funkcija: povezi na wifi, vrne true/false za uspeh
bool connectToWifiAccessPoint(){
  DEBUG_PRINTLN("Inicializiram Wifi modul");
  
  // preveri ce ima plosca wifi modul
  if (WiFi.status() == WL_NO_MODULE) {
    DEBUG_PRINTLN("Wifi modul ni bil najden");
    DEBUG_PRINTLN("Program se ne bo nadaljeval...");
    while(true){
      ;
    }
  }

  // opomba, ce firmware ni na zadnji verziji
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    DEBUG_PRINTLN("Posodobi firmware za Wifi modul!");
    DEBUG_PRINTLN("Najnovejsa: " + String(WIFI_FIRMWARE_LATEST_VERSION) + ", trenutna: " + fv);
  }

  // povezi se na wifi dostopno tocko
  int status = WL_IDLE_STATUS, retries = 0;
  while (status != WL_CONNECTED) {
    DEBUG_PRINTLN("Poskus povezave na omrezje z WPA SSID: ");
    DEBUG_PRINTLN(WIFI_SSID);
    status = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // povezava na wifi je veckrat spodletela, prekini in vrni false
    if(retries > MAX_RETRIES_CONNECT_WIFI){
      return false;
    }

    retries++;
  }

  // nekaj delaya, da se vzpostavi povezava
  delay(5000);

  DEBUG_PRINTLN("Naprava povezana na omrezje\n");

  DEBUG_PRINTLN("IP naslov: ");
  DEBUG_PRINTLN(WiFi.localIP());

  return true;
}

// funkcija: vzpostavi wifi povezavo z dostopno tocko(AP)
void powerOffWIFI(){
  WiFi.disconnect();
  WiFi.end();
}

// funkcija: pridobi GMT (greenwich cas) iz streznika
unsigned long getServerGMT(int maxTries = 10){
  unsigned long secondsSinceEpoch = 0;
  int tries = 0;
  do{
    // vrne sekunde od 1970
    secondsSinceEpoch = WiFi.getTime();
    tries++;
  }
  while(secondsSinceEpoch == 0 && tries < maxTries);

  return secondsSinceEpoch;
}

// funkcija: shrani JSON meritev v firebaseDB, vrne prazen string za uspeh
String storeMeasurmentToDatabse(float temp, float humidity, String timestamp){
  String dataPath = "/";
  String jsonData = "{\""+String(timestamp)+"\":{\"temperature\":"+String(temp)+",\"humidity\":"+String(humidity)+"}}";
  DEBUG_PRINTLN("Poslan JSON:" + jsonData);

  if(Firebase.updateNode(firebaseData, dataPath, jsonData)){
    DEBUG_PRINTLN("POT: " + firebaseData.dataPath());
    DEBUG_PRINTLN("TIP: " + firebaseData.dataType()); // pricakovan JSON
    DEBUG_PRINTLN("PODATKI: " + firebaseData.jsonData()); // moralo se bi ujemati z jsonData

    return ""; // uspesno
  }
  else{
    DEBUG_PRINTLN("Napaka pri posiljanju v bazo:");
    DEBUG_PRINTLN(firebaseData.errorReason());

    return firebaseData.errorReason(); // neuspesno
  }
}

// funkcija: iz polj za napake naredi json
String genErrorJson(){
  String jsonData = "";

  for(int i = 0; i <= errorIndex; i++){
    String errorType = "";
    if(errorTypes[i] == true){ errorType = "wifi"; }
    else{ errorType = "firebase"; }

    jsonData += "{\""+String(errorDates[i])+"\":"+errorType+"}";
    if(i < errorIndex){
      jsonData += ", ";
    }
  }

  return jsonData;
}

// funkcija: shrani napako (wifi down, firebase povezava neuspesna) v bazo (pot /napake)
bool addErrorMesageToDatabase(String timestamp){
  String dataPath = "/napake";
  String jsonData = genErrorJson();
  DEBUG_PRINTLN("Poslan JSON:" + jsonData);

  // pocisti errorje
  errorIndex = -1;

  if(Firebase.updateNode(firebaseData, dataPath, jsonData)){
    DEBUG_PRINTLN("POT: " + firebaseData.dataPath());
    DEBUG_PRINTLN("TIP: " + firebaseData.dataType()); // pricakovan JSON
    DEBUG_PRINTLN("PODATKI: " + firebaseData.jsonData()); // moralo se bi ujemati z jsonData

    return true;
  }
  else{
    DEBUG_PRINTLN("Napaka pri posiljanju v bazo:");
    DEBUG_PRINTLN(firebaseData.errorReason());

    return false;
  }
}

// funkcija: callback, ki se klice ko se plosca zbudi iz spanja
void alarmCallback(){
  nextHours = (rtc.getHours() + 1) % 24;
  nextMinutes = 0;
  nextSeconds = 0;

  rtc.setAlarmTime(nextHours, nextMinutes, nextSeconds);
}

void setup() {
  DEBUG_START_SERIAL();

  // zacni RTC
  rtc.begin();

  // aktiviraj wifi modul in povezi se na omrezje
  if(!connectToWifiAccessPoint()){
    // povezava na wifi spodletela
    // ne nadaljuj programa, ker ne morem nastavit rtc ure iz neta, prizgi oranzni led
    DEBUG_PRINTLN("Povezava na wifi spodletela, pridobitev ure iz streznika ni mogoca, program se bo koncal...");
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    while(true){
      ;
    }
  }

  // shrani firebase crediantiale v objekt
  Firebase.begin(FIREBASE_PROJECT_ID, FIREBASE_SECRET, WIFI_SSID, WIFI_PASSWORD);
  Firebase.reconnectWiFi(true);

  // pridobi GMT iz streznika
  unsigned long secondsSinceEpoch = getServerGMT();
  if(secondsSinceEpoch == 0){
    DEBUG_PRINTLN("Neuspesna pridobitev NTP serverja...");
  }
  else{ // uspesna pridobitev casa s serverja
    // nastavi cas v RTC modulu
    rtc.setEpoch(secondsSinceEpoch);
    // popravi casovno xono
    rtc.setHours(rtc.getHours() + offsetGMT);
  }

  // omogoci RTC alarm, ujemanje ure, minute in sekunde
  rtc.enableAlarm(rtc.MATCH_HHMMSS);

  // dodaj prekinitev, ki se klice ob alarmu
  rtc.attachInterrupt(alarmCallback);

  // nastavi cas alarma
  alarmCallback();
}

void loop() {
  // ugasni wifi modul
  powerOffWIFI();

  // standby do alarma (ZAKOMENTIRAJ, DA BO DELOVAL SERIAL PORT)
  DEBUG_PRINTLN("Naprava gre v spanje...");
  rtc.standbyMode();
  // ODKOMENTIRAJ, DA BO DELOVAL SERIAL PORT!
  // delay(9000);
  // alarmCallback();

  // trenutni cas v stringu
  String timestamp = String(rtc.getHours()) + "-" + String(rtc.getMinutes()) + "-" + String(rtc.getSeconds());

  // zacni serial (ce je debug makro omogocen)
  DEBUG_START_SERIAL();

  // aktiviraj wifi modul in se povezi na omrezje
  if(connectToWifiAccessPoint()){
    // uspesna povezava na wifi
    float temp = 7.4, humidity = 83.0;

    String firebaseError = storeMeasurmentToDatabse(temp, humidity, timestamp);
    if(firebaseError == ""){
      // uspesen upload

      // poslji prejsnje napake(ce so bile)
      if(errorIndex != -1){
      addErrorMesageToDatabase(timestamp);
      }
    }

    else{
      // v primeru napake pri povezavi na bazo shrani error s timestampom
      DEBUG_PRINTLN("Upload na firebase spodletel...");
      errorDates[++errorIndex] = rtc.getEpoch();
      errorTypes[++errorIndex] = false;
    }
  }
  else{
    // povezava na wifi spodletela
    DEBUG_PRINTLN("Povezava na wifi spodletela...");
    errorDates[++errorIndex] = rtc.getEpoch();
    errorTypes[++errorIndex] = true;
  }
}