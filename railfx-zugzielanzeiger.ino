#include <Wire.h>                             // Bibliothek für die I2C Funktionalität
#include "SSD1306Ascii.h"                     // Bibliothek für die Displays
#include "SSD1306AsciiWire.h"                 // Bibliothek für die Displays
#include <dummy.h>                            // Bibliothek für ESP32
#include <DBAPI.h>                            // Bibliothek um die Deutsche Bahn API abzufragen

/*
     Rail-FX Live-Zugzielanzeiger der Deutschen Bahn
     StartHardware.org
*/

/* ***** ***** Einstellungen ***** ***** ***** *****  ***** ***** ***** *****  ***** ***** ***** ***** */

const char* ssid = "wifi-name";                  // WiFi-Name
const char* password = "wifi-password";           // WiFi-Passwort
const char* bahnhofsName = "Berlin Hbf";      // Eine Liste findet man hier: https://data.deutschebahn.com/dataset/data-haltestellen.html
String removeString1 = "Berlin ";             // Dieser String wird vom Bahnhofsnamen entfernt, z.B. "Berlin " von "Berlin Warschauer Straße"
String removeString2 = "Berlin-";             // Dieser String wird vom Bahnhofsnamen entfernt, z.B. "Berlin-" von "Berlin-Westkreuz"
int showPlatform[8] = {15, 16, 1, 2, 5, 6, 7, 8};  // Zuordnung der Displays zu den Gleisen (Display 1 = Gleis 15, Display 2 = Gleis 16 ... Display 8 = Gleis 8

long anzeigeTimeout = 20000;                  // Anzeigewechsel alle x Millisekunden
int apiCallTimeout = 20000;                   // Pause in Millisekunden zwischen den API Calls der DB API – Abrufen der Abfahrtszeiten alle x Millisekunden

int maxPlatforms = 20;

/* ***** ***** Ab hier beginnt der Programmcode, der nicht oder wenig angepasst werden muss ***** ***** ***** ***** */

DBAPI db;                                      // Bahn API Objekt
#define I2C_ADDRESS 0x3C                       // Adresse der OLEDs

/* Speicher Variablen */
char* theDates[250];                           // Array, um Abfahrtsdaten zu speichern
char* theTimes[250];                           // Array, um Abfahrtsdaten zu speichern
char* theProducts[250];                        // Array, um Abfahrtsdaten zu speichern
String theTargets[250];                        // Array, um Abfahrtsdaten zu speichern
String thePlatforms[250];                      // Array, um Abfahrtsdaten zu speichern
char* theTextdelays[250];                      // Array, um Abfahrtsdaten zu speichern
int myIndex = 0;                               // Anzahl der Einträge im Array

const char* ntpServer = "pool.ntp.org";        // Network Time Protokol Server-Adresse
const long  gmtOffset_sec = 3600;             // Offset für Zeitzone (3600 = CET)
const int   daylightOffset_sec = 3600;        // Offset für Sommerzeit (3600 = Sommerzeit)


int myDay;                                    // Aktueller Tag
int myMonth;                                  // Aktueller Monat
int myYear;                                   // Aktuelles Jahr
int myHour;                                   // Aktuelle Stunde
int myMinute;                                 // Aktuelle Minute

/* Timer Variablen */
long anzeigeTimer = 0;
long apiCallTime;

/* Variablen für das OLED */
SSD1306AsciiWire oled;

void setup() {
  Wire.begin();                              // I2C Verbindung zum OLED
  Wire.setClock(400000L);                    // I2C Verbindung zum OLED
  for (int i = 0; i < 7; i++) {
    TCA9548A(i);
    oled.begin(&Adafruit128x32, I2C_ADDRESS);  // Start des OLEDs
  }

  Serial.begin(115200);                      // started die serielle Kommunikation
  WiFi.mode(WIFI_STA);                       // Setzt den WIFI-Modus
  WiFi.begin(ssid, password);                // Startet die WIFI Verbindung
  while (WiFi.status() != WL_CONNECTED) {
    Serial.write('.');
    delay(500);
  }

  drawTest(0);   // Tafel an I2C Adresse 0 des TCA9548A, Test
  drawTest(1);   // Tafel an I2C Adresse 0 des TCA9548A, Test
  drawTest(2);   // Tafel an I2C Adresse 0 des TCA9548A, Test
  drawTest(3);   // Tafel an I2C Adresse 0 des TCA9548A, Test

  DBstation* station = db.getStation(bahnhofsName); // Setzt den Bahnhof für die DB Api

  //yield();
  if (station != NULL) {
    Serial.println();
    Serial.print("Name:      ");
    Serial.println(station->name);
    Serial.print("ID:        ");
    Serial.println(station->stationId);
    Serial.print("Latitude:  ");
    Serial.println(station->latitude);
    Serial.print("Longitude: ");
    Serial.println(station->longitude);
  }
  callDBApi();
  anzeigeTimer -= anzeigeTimeout;
}

void loop() {
  //yield();
  if (apiCallTime + apiCallTimeout < millis()) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getLocalTime();
    //delay(100);
    callDBApi();
    apiCallTime = millis();
  }

  anzeigeTafel();
}

void anzeigeTafel() {
  if (anzeigeTimer + anzeigeTimeout < millis()) { // der zweite Teil verhindert, dass ein Anzeigewechsel dem Empfang des Zeitsignals blockiert
    anzeigeTimer = millis();
    for (int i = 0; i < 8; i++) {
      drawInfo(i, showPlatform[i]); // Tafeln an I2C Adressen 0 - 7 des TCA9548A
    }
  }
}

void drawInfo(uint8_t displayNr, int platformToDisplay) {     // OLED Ausgabe
  Serial.println("Draw: "); Serial.println(displayNr);
  TCA9548A(displayNr);
  int theEntryIndexes[] = {-1,-1,-1,-1};
  int theEntryIndexesIndex = 0;

  for (int j = 0; j < myIndex; j++) {
    if (theEntryIndexesIndex < 4) {
      if (thePlatforms[j].toInt() == platformToDisplay) {
        theEntryIndexes[theEntryIndexesIndex] = j;
        Serial.print(displayNr); Serial.print(" <- Display \t ");Serial.print(j); Serial.print(" <- Index \t ");Serial.println(thePlatforms[j].toInt());
        theEntryIndexesIndex++;
      }
    }
  }

  oled.clear();
  //oled.setInvertMode(0);
  oled.setFont(Adafruit5x7);
  for (int i = 0; i < 4; i++) {
    if (theEntryIndexes[i] != -1) {
      oled.setCursor(0 , i); oled.println(theTimeDifference(theTimes[theEntryIndexes[i]], theDates[theEntryIndexes[i]]));
      oled.setCursor(20 , i); oled.println(theTargets[theEntryIndexes[i]]);

    }
  }
  delay(100);
}

void drawTest(uint8_t displayNr) {     // OLED Ausgabe
  Serial.println("DrawTest: "); Serial.println(displayNr);
  TCA9548A(displayNr);
  oled.clear();
  //oled.setInvertMode(0);
  oled.setFont(Adafruit5x7);
  oled.setCursor(0 , 0); oled.println("RailFX");
  oled.setCursor(0 , 1); oled.println("RailFX");
  oled.setCursor(0 , 2); oled.println("RailFX");
  oled.setCursor(0 , 3); oled.println("RailFX");
  oled.setCursor(30 , 0); oled.println("StartHardware");
  oled.setCursor(30 , 1); oled.println("StartHardware");
  oled.setCursor(30 , 2); oled.println("StartHardware");
  oled.setCursor(30 , 3); oled.println("StartHardware");
}

void TCA9548A(uint8_t bus) {
  if (bus > 7) return;           // Falls Input zu groß, abbrechen
  Wire.beginTransmission(0x70);  // TCA9548A address is 0x70
  Wire.write(1 << bus);          // send byte to select bus
  Wire.endTransmission();
}

void getLocalTime() {                         // Aktuelle Zeit abfragen
  time_t now = time(NULL);
  struct tm *tm_struct = localtime(&now);
  myDay = tm_struct->tm_mday;
  myMonth = tm_struct->tm_mon + 1;
  myYear = tm_struct->tm_year + 1900;
  myHour = tm_struct->tm_hour;
  myMinute = tm_struct->tm_min;
}

int theTimeDifference(char* theDepartureTime, char* theDepartureDate) {
  int timeToTrain = 0;

  int theDepartureHour, theDepartureMinute;
  sscanf(theDepartureTime, "%d:%d", &theDepartureHour, &theDepartureMinute);
  int theDepartureYear, theDepartureMonth, theDepartureDay;
  sscanf(theDepartureDate, "%d.%d.%d", &theDepartureDay, &theDepartureMonth, &theDepartureYear);

  boolean departureTomorrow = false;
  if (theDepartureYear - 2000 > myYear) {
    departureTomorrow = true;
  } else if (theDepartureMonth > myMonth) {
    departureTomorrow = true;
  } else if (theDepartureDay > myDay) {
    departureTomorrow = true;
  }

  if (departureTomorrow == true) {
    timeToTrain = ((theDepartureHour + 24) * 60 + theDepartureMinute) - (myHour * 60 + myMinute);
  } else {
    timeToTrain = (theDepartureHour * 60 + theDepartureMinute) - (myHour * 60 + myMinute);
  }
  return timeToTrain;
}

void callDBApi() {
  Serial.println("Call DB API");
  DBstation* station = db.getStation(bahnhofsName);
  myIndex = 0;
  DBdeparr* da = db.getDepartures(station->stationId, NULL, NULL, NULL, 0, PROD_ICE | PROD_IC_EC | PROD_IR | PROD_RE | PROD_S);
  char myDate;
  while (da != NULL) {
    yield();
    theDates[myIndex] = da->date;
    theTimes[myIndex] = da->time;
    theProducts[myIndex] = da->product;
    theTargets[myIndex] = da->target;
    thePlatforms[myIndex] = da->platform;
    theTextdelays[myIndex] = da->textdelay;
    da = da->next;
    myIndex++;
  }

  for (int platform = 0; platform < maxPlatforms; platform++) {
    for (int i = 0; i < myIndex; i++) {
      if ( thePlatforms[i].toInt() == platform) {
        //Serial.print(theDates[i]); Serial.print("\t");
        Serial.print(i); Serial.print("\t");
        Serial.print(theTimes[i]); Serial.print("\t");
        Serial.print(theProducts[i]); Serial.print("\t");
        Serial.print(thePlatforms[i]); Serial.print("\t");
        Serial.print(theTextdelays[i]); Serial.print("\t");
        theTargets[i].replace(removeString1, "");
        theTargets[i].replace(removeString2, "");
        Serial.print(theTargets[i]); Serial.println("");
      }
    }
  }
  Serial.println(""); Serial.println("");
  Serial.println("");
}
