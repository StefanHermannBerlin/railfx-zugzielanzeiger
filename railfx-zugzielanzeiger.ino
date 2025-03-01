#include <WiFi.h>                             // WiFi-Funktionalität
#include <Wire.h>                             // I2C-Funktionalität
#include "SSD1306Ascii.h"                     // OLED-Display (Adafruit GFX)
#include "SSD1306AsciiWire.h"                 // OLED-Display (I2C)
#include <DBAPI.h>                            // Schnittstelle zur Deutschen Bahn API

/*
    Rail-FX Live-Zugzielanzeiger der Deutschen Bahn
    StartHardware.org
*/

/* ***** Einstellungen ***** */
const char* ssid            = "wifi-name";       // WiFi-Name
const char* password        = "wifi-password";    // WiFi-Passwort
const char* bahnhofsName    = "Berlin Hbf";   // Beispielstation (Liste z. B. unter https://data.deutschebahn.com/dataset/data-haltestellen.html)
String removeString1        = "Berlin ";      // Zu entfernender String
String removeString2        = "Berlin-";      // Zu entfernender String
int showPlatform[8]         = {15, 16, 1, 2, 5, 6, 7, 8}; // Display-Gleis-Zuordnung

long anzeigeTimeout         = 10000;          // Anzeigewechselintervall (ms)
int apiCallTimeout          = 20000;          // Pause zwischen API-Abfragen (ms)
int maxPlatforms            = 20;

/* ***** Globale Objekte und Variablen ***** */
DBAPI db;                                      // DB API Objekt
#define I2C_ADDRESS 0x3C                       // OLED I2C Adresse

String theDates[250];       // Datum-Strings (Format: TT.MM.JJJJ)
String theTimes[250];       // Uhrzeit-Strings (Format: HH:MM)
String theProducts[250];    // Zugtyp (z. B. ICE)
String theTargets[250];     // Zielhaltestelle
String thePlatforms[250];   // Gleisnummer als String
String theTextdelays[250];  // Verspätung als Text
int myIndex = 0;            // Anzahl der erfassten Einträge

const char* ntpServer       = "pool.ntp.org"; // NTP-Server
const long  gmtOffset_sec   = 3600;           // Zeitzonenoffset (CET)
const int   daylightOffset_sec = 0;           // Sommerzeit-Offset

// Aktuelle Zeitvariablen
int myDay, myMonth, myYear, myHour, myMinute;

// Timer Variablen
long anzeigeTimer = 0;
long apiCallTime  = 0;

// OLED Objekt
SSD1306AsciiWire oled;

/* Funktionsprototypen */
void TCA9548A(uint8_t bus);
void drawTest(uint8_t displayNr);
void drawInfo(uint8_t displayNr, int platformToDisplay);
void anzeigeTafel();
void getLocalTime();
int theTimeDifference(String theDepartureTime, String theDepartureDate);
void callDBApi();

void setup() {
  Wire.begin();
  Wire.setClock(400000L);
  
  // Initialisiere alle 8 Displays (falls alle angeschlossen sind)
  for (int i = 0; i < 8; i++) {
    TCA9548A(i);
    oled.begin(&Adafruit128x32, I2C_ADDRESS);
  }

  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.write('.');
    delay(500);
  }

  // Führe einen kurzen Test auf einigen Displays durch
  for (int i = 0; i < 4; i++) {
    drawTest(i);
  }

  // Abfrage der Bahnhofsdaten
  DBstation* station = db.getStation(bahnhofsName);
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
  anzeigeTimer = millis() - anzeigeTimeout; // sofortige erste Aktualisierung der Anzeige
}

void loop() {
  if (apiCallTime + apiCallTimeout < millis()) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getLocalTime();
    callDBApi();
    apiCallTime = millis();
  }
  anzeigeTafel();
}

/* ===== Anzeige-Funktionen ===== */
void anzeigeTafel() {
  if (anzeigeTimer + anzeigeTimeout < millis()) {
    anzeigeTimer = millis();
    // Aktualisiere alle 8 Displays (Index 0 bis 7)
    for (int i = 0; i < 8; i++) {
      drawInfo(i, showPlatform[i]);
    }
  }
}

void drawInfo(uint8_t displayNr, int platformToDisplay) {
  Serial.print("Draw: ");
  Serial.println(displayNr);
  TCA9548A(displayNr);
  
  // Ermittle bis zu 4 Einträge, die diesem Gleis zugeordnet sind
  int theEntryIndexes[4] = {-1, -1, -1, -1};
  int idx = 0;
  for (int j = 0; j < myIndex && idx < 4; j++) {
    if (thePlatforms[j].toInt() == platformToDisplay) {
      theEntryIndexes[idx++] = j;
      Serial.print(displayNr);
      Serial.print(" <- Display \t ");
      Serial.print(j);
      Serial.print(" <- Index \t ");
      Serial.println(thePlatforms[j].toInt());
    }
  }

  oled.clear();
  oled.setFont(Adafruit5x7);
  for (int i = 0; i < 4; i++) {
    if (theEntryIndexes[i] != -1) {
      oled.setCursor(0, i);
      oled.println(theTimeDifference(theTimes[theEntryIndexes[i]], theDates[theEntryIndexes[i]]));
      oled.setCursor(20, i);
      oled.println(theTargets[theEntryIndexes[i]]);
    }
  }
  delay(100);
}

void drawTest(uint8_t displayNr) {
  Serial.print("DrawTest: ");
  Serial.println(displayNr);
  TCA9548A(displayNr);
  oled.clear();
  oled.setFont(Adafruit5x7);
  // Zeige Test-Text an
  oled.setCursor(0, 0); oled.println("RailFX");
  oled.setCursor(0, 1); oled.println("RailFX");
  oled.setCursor(0, 2); oled.println("RailFX");
  oled.setCursor(0, 3); oled.println("RailFX");
  oled.setCursor(30, 0); oled.println("StartHardware");
  oled.setCursor(30, 1); oled.println("StartHardware");
  oled.setCursor(30, 2); oled.println("StartHardware");
  oled.setCursor(30, 3); oled.println("StartHardware");
}

/* ===== I2C-Multiplexer ===== */
void TCA9548A(uint8_t bus) {
  if (bus > 7) return;
  Wire.beginTransmission(0x70);
  Wire.write(1 << bus);
  Wire.endTransmission();
}

/* ===== Zeitfunktionen ===== */
void getLocalTime() {
  time_t nowTime = time(nullptr);
  struct tm *tm_struct = localtime(&nowTime);
  myDay    = tm_struct->tm_mday;
  myMonth  = tm_struct->tm_mon + 1;
  myYear   = tm_struct->tm_year + 1900;
  myHour   = tm_struct->tm_hour;
  myMinute = tm_struct->tm_min;
}

// Gibt die Differenz in Minuten zwischen Abfahrtszeit und aktueller Zeit zurück
int theTimeDifference(String theDepartureTime, String theDepartureDate) {
  int depHour, depMinute, depDay, depMonth, depYear;
  // Lese Abfahrtszeit und Datum aus den Strings
  sscanf(theDepartureTime.c_str(), "%d:%d", &depHour, &depMinute);
  sscanf(theDepartureDate.c_str(), "%d.%d.%d", &depDay, &depMonth, &depYear);

  // Debug-Ausgaben für die Eingangsdaten und die geparsten Werte
  Serial.print("DEBUG: theDepartureTime = ");
  Serial.println(theDepartureTime);
  Serial.print("DEBUG: theDepartureDate = ");
  Serial.println(theDepartureDate);
  Serial.print("DEBUG: Parsed departure time = ");
  Serial.print(depHour);
  Serial.print(":");
  Serial.println(depMinute);
  Serial.print("DEBUG: Parsed departure date = ");
  Serial.print(depDay);
  Serial.print(".");
  Serial.print(depMonth);
  Serial.print(".");
  Serial.println(depYear);

  // Bestimme, ob die Abfahrt morgen ist
  bool departureTomorrow = (depYear > myYear) ||
                           (depYear == myYear && depMonth > myMonth) ||
                           (depYear == myYear && depMonth == myMonth && depDay > myDay);

  // Berechne die aktuelle Zeit in Minuten
  int currentMinutes = myHour * 60 + myMinute;
  // Berechne die Abfahrtszeit in Minuten
  int departureMinutes = depHour * 60 + depMinute;

  if (departureTomorrow) {
    Serial.println("DEBUG: Departure is tomorrow, adding 24*60 minutes");
    departureMinutes += 24 * 60;
  }

  int diff = departureMinutes - currentMinutes;
  Serial.print("DEBUG: Final departureMinutes = ");
  Serial.println(departureMinutes);
  Serial.print("DEBUG: Time difference = ");
  Serial.println(diff);

  return diff;
}

/* ===== API-Aufruf ===== */
void callDBApi() {
  Serial.println("Call DB API");
  DBstation* station = db.getStation(bahnhofsName);
  myIndex = 0;
  // Abrufen der Abfahrtsdaten (mit 40 Ergebnissen, 1 Stunde Dauer, gewünschte Verkehrsmittel)
  DBdeparr* da = db.getDepartures(station->stationId, NULL, NULL, 20, 1,
                                    PROD_ICE | PROD_IC_EC | PROD_IR | PROD_RE | PROD_S);
  
  while (da != NULL && myIndex < 250) {
    yield();
    
    // Konvertiere den time_t-Wert in Datum und Uhrzeit als String
    char dateBuffer[20];
    char timeBuffer[10];
    //struct tm *tm_info = localtime(&da->time);
    struct tm *tm_info = gmtime(&da->time);
    strftime(dateBuffer, sizeof(dateBuffer), "%d.%m.%Y", tm_info);
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", tm_info);

    theDates[myIndex]    = String(dateBuffer);
    theTimes[myIndex]    = String(timeBuffer);
    theProducts[myIndex] = String(da->product);
    theTargets[myIndex]  = String(da->target);
    thePlatforms[myIndex]= String(da->platform);
    theTextdelays[myIndex] = String(da->delay)+ " min";
    
    da = da->next;
    myIndex++;
  }

  // Ausgabe der abgerufenen Daten über Serial
  for (int platform = 0; platform < maxPlatforms; platform++) {
    for (int i = 0; i < myIndex; i++) {
      if (thePlatforms[i].toInt() == platform) {
        Serial.print(i); Serial.print("\t");
        Serial.print(theTimes[i]); Serial.print("\t");
        Serial.print(theProducts[i]); Serial.print("\t");
        Serial.print(thePlatforms[i]); Serial.print("\t");
        Serial.print(theTextdelays[i]); Serial.print("\t");
        // Entferne überflüssige Zeichen aus dem Zielnamen
        String targetClean = theTargets[i];
        targetClean.replace(removeString1, "");
        targetClean.replace(removeString2, "");
        Serial.println(targetClean);
      }
    }
  }
  Serial.println("\n\n");
}
