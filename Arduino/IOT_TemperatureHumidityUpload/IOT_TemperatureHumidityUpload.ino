// vnaprej pripravljene knjiznjice
#include "Firebase_Arduino_WiFiNINA.h"
#include <WiFiNINA.h>
#include <RTCZero.h>

// moja lastna knjiznjica
#include "DHT22.h"

// wifi, firebase avtorizacija (skrito)
#include "mySecrets.h"

// za izpis printov v "debug mode"
//#define DEBUGGING_ENABLED // ZAKOMENTIRAJ ZA "PRODUCTION MODE"

#ifdef DEBUGGING_ENABLED
  #define DEBUG_START_SERIAL() startSerial()
  #define DEBUG_PRINT(x)  Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_START_SERIAL()
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#define FIREBASE_PATH_MEASURMENTS "/measurments"
#define FIREBASE_PATH_ERRORS "/errors"

// maximalno stevilo poskusov povezave na wifi ob neuspehu (potem preneha)
const static int MAX_RETRIES_CONNECT_WIFI = 5;

// pin, kjer se nahaja onboard led
const static pin_size_t LED_PIN = 6;

// stevilo uspesnih meritev senzorja DHT 22
const static int NUM_OF_MEASURMENTS = 10;
// timeout neuspesnega merjenja
const static unsigned long MEASURMENT_TIMEOUT = 7 * 60 * 1000; // [ms]

// objekt firebase
FirebaseData firebaseDataObj;
// RTC objekt
RTCZero rtc;

// GMT time zone + poletni cas
const static int GMT = 2;
const static int YEAR_OFFSET = 2000;

// globalne spremenljivke
uint8_t nextHours = 0;
uint8_t nextMinutes = 0;
uint8_t nextSeconds = 0;
uint8_t offsetGMT = 2; // +1 (casovna cona) +1 (poletni cas)

// belezenje neuspehov povezave, shranim tudi meritve, ki jih potem z zamikom vnesem v bazo
float errorTemperatures[100], errorHumidities[100]; // meritve
String errorDates [100]; // ze v timestamp formatu
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

// funkcija: pretvori in vrne cas v stringu ISO formata (npr. 2017-01-30T12:30)
String formatTimeStringISO(){
  char timestamp[16];
  uint8_t minutes = 0;
  // %02u -> (unsigned) doda vodilno niclo, da so vedno dve cifri
  sprintf(timestamp, "%u-%02u-%02uT%02u:%02u", rtc.getYear() + YEAR_OFFSET, rtc.getMonth(), rtc.getDay(), rtc.getHours(), minutes);
  
  return String(timestamp);
}

// funkcija: poslji (updateNode) JSON v firebaseDB, vrne prazen string za uspeh
String sendJsonToDatabse(String jsonData, String dataPath){
  DEBUG_PRINTLN("Poslan JSON:" + jsonData);

  if(Firebase.updateNode(firebaseDataObj, dataPath, jsonData)){
    DEBUG_PRINTLN("POT: " + firebaseDataObj.dataPath());
    DEBUG_PRINTLN("TIP: " + firebaseDataObj.dataType()); // pricakovan JSON
    DEBUG_PRINTLN("PODATKI: " + firebaseDataObj.jsonData()); // moralo se bi ujemati z jsonData

    return ""; // uspesno
  }
  else{
    DEBUG_PRINTLN("Napaka pri posiljanju v bazo:");
    DEBUG_PRINTLN(firebaseDataObj.errorReason());

    return firebaseDataObj.errorReason(); // neuspesno
  }
}

// funkcija: shrani JSON meritev v firebaseDB, vrne prazen string za uspeh
String storeMeasurment(float temperature, float humidity, String timestamp){
  String dataPath = FIREBASE_PATH_MEASURMENTS;
  String jsonData = "{\""+timestamp+"\":{\"temperature\":"+String(temperature)+",\"humidity\":"+String(humidity)+"}}";
  String dbResponse = sendJsonToDatabse(jsonData, dataPath);
  
  return dbResponse;
}

// funkcija: iz polj (vecih) meritev naredi json
String genMultipleMeasurmentsJson(){
  String jsonData = "{";

  for(int i = 0; i <= errorIndex; i++){

    jsonData += "\""+errorDates[i]+"\":{\"temperature\":"+String(errorTemperatures[i])+",\"humidity\":"+String(errorHumidities[i])+"}";
    if(i < errorIndex){
      jsonData += ", ";
    }
  }

  jsonData += "}";
  return jsonData;
}

// funkcija: shrani JSON meritve (ki prej niso bile uspesno poslane) v firebaseDB, vrne prazen string za uspeh
String storeFailedUploadedMeasurments(){
  String dataPath = FIREBASE_PATH_MEASURMENTS;
  String jsonData = genMultipleMeasurmentsJson();
  String dbResponse = sendJsonToDatabse(jsonData, dataPath);
  
  return dbResponse;
}

// funkcija: iz polj za napake naredi json
String genErrorsJson(){
  String jsonData = "{";

  for(int i = 0; i <= errorIndex; i++){
    String errorType = "";
    if(errorTypes[i] == true){ errorType = "wifi"; }
    else{ errorType = "firebase"; }

    jsonData += "\""+errorDates[i]+"\":\""+errorType+"\"";
    if(i < errorIndex){
      jsonData += ", ";
    }
  }

  jsonData += "}";
  return jsonData;
}

// funkcija: shrani napako (napaka branja senzorja, wifi down, firebase povezava neuspesna) v bazo, vrne prazen string za uspeh
String storeErrorMesages(bool isFailedSensorReading = false, String timestamp = ""){
  String dataPath = FIREBASE_PATH_ERRORS;

  String jsonData = "";
  if(isFailedSensorReading){
    // napaka pri branju senzorja
    jsonData = "{\""+timestamp+"\":\"dht22\"}";
  }
  else{
    // firebase, wifi napake (ki so shranjene v array)
    jsonData = genErrorsJson();
  }

  String dbResponse = sendJsonToDatabse(jsonData, dataPath);
  
  return dbResponse;
}

// funkcija: dodaj podatke v polje ob napaki na netu ali uploadu v bazo
void addFailedUploadedMeasurments(float temperature, float humidity, bool isWifiError){
  errorTemperatures[++errorIndex] = temperature;
  errorHumidities[errorIndex] = humidity;
  errorDates[errorIndex] = formatTimeStringISO();
  errorTypes[errorIndex] = isWifiError;
}

// funkcija: callback, ki se klice ko se plosca zbudi iz spanja
void alarmCallback(){
  // nastavi na eno uro vnaprej
  nextHours = (rtc.getHours() + 1) % 24;
  nextMinutes = 0;
  nextSeconds = 0;

  // nastavi alarm oziroma cas zbujanja plosce iz spanja
  rtc.setAlarmTime(nextHours, nextMinutes, nextSeconds);
}

// funkcija: vrne povprecno temperaturo in vlaznost po 10 uspesnih meritvah (true/false za uspesnost)
bool getTemperatureAndHumidity(float &avgTemperature, float &avgHumidity){
  int currRead = 0;
  float t = 0.0, h = 0.0, temperatureArr[NUM_OF_MEASURMENTS], humidityArr[NUM_OF_MEASURMENTS];
  unsigned long startTime = millis();

  // mikrokrmilnik mora dobiti NUM_OF_MEASURMENTS uspesnih meritev, dokler ne pretece MEASURMENT_TIMEOUT
  while(currRead < NUM_OF_MEASURMENTS && millis() - startTime < MEASURMENT_TIMEOUT){
    if(readDHT(h, t)){
      // uspesno prebrano, shrani v polje in povecaj indeks
      temperatureArr[currRead] = t;
      humidityArr[currRead] = h;
      currRead++;
      DEBUG_PRINTLN("Uspesna meritev stevilka: " + String(currRead));
    }

    // pocakaj MIN_MEASURMENT_INTERVAL do naslednje meritve
    delay(MIN_MEASURMENT_INTERVAL);

  }

  if(millis() - startTime >= MEASURMENT_TIMEOUT){
    // MEASURMENT_TIMEOUT potekel, vrni false
    DEBUG_PRINTLN("Branje iz senzorja traja dlje od MEASURMENT_TIMEOUT, meritev ni bila uspesna...");
    return false;
  }

  // izracun povprecja meritev
  t = 0.0; h = 0.0;
  for(int i = 0; i < NUM_OF_MEASURMENTS; i++){
    t += temperatureArr[i];
    h += humidityArr[i];
  }
  
  avgTemperature = t / NUM_OF_MEASURMENTS;
  avgHumidity = h / NUM_OF_MEASURMENTS;
  
  return true;
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
    // popravi casovno cono
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

  // standby do alarma (ZAKOMENTIRAJ rtc.standbyMode();, DA BO DELOVAL SERIAL PORT)
  DEBUG_PRINTLN("Naprava gre v spanje...");
  rtc.standbyMode();
  delay(5000);

  // pridobi ISO time string
  String timestamp = formatTimeStringISO();

  // zacni serial (ce je debug makro omogocen)
  DEBUG_START_SERIAL();
  //pridobi meritve senzorja
  DEBUG_PRINTLN("Poskus pridobitve meritev s senzorja...");
  float temperature = 0.0, humidity = 0.0;
  bool sensorReadingsSuccessful = getTemperatureAndHumidity(temperature, humidity);

  // aktiviraj wifi modul in se povezi na omrezje
  if(connectToWifiAccessPoint()){
    // uspesna povezava na wifi

    if(sensorReadingsSuccessful){
      // uspesna pridobitev meritev s senzorja

      // poslji v oblacno bazo
      String firebaseError = storeMeasurment(temperature, humidity, timestamp);
      if(firebaseError == ""){
        // uspesen upload
        DEBUG_PRINTLN("Uspesno poslal podatke v bazo");

        // poslji prejsnje neuspele meritve in napake(ce so bile)
        if(errorIndex != -1){
        DEBUG_PRINTLN("Posiljanje zaostalih meritev v bazo");
        storeFailedUploadedMeasurments();
        storeErrorMesages();

        // resetiraj indeks napak
        errorIndex = -1;
        }
      }

      else{
        // v primeru napake pri povezavi na bazo shrani podatke v polje
        DEBUG_PRINTLN("Upload na firebase spodletel...");
        addFailedUploadedMeasurments(temperature, humidity, false); // false = db napaka
      }
    }

    else{
      // neuspesna pridobitev meritev s senzorja, poslji napako v bazo
      storeErrorMesages(true, timestamp);
    }
  }
  else{
    // povezava na wifi spodletela, shrani podatke v polje
    DEBUG_PRINTLN("Povezava na wifi spodletela...");
    addFailedUploadedMeasurments(temperature, humidity, true); // true = wifi napaka
  }
}