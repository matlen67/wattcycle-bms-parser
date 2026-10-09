/***************************************************************************
  WattCycle BMS Parser @matlen67
  Version: 1.0

  ----------------------------
  Wiring: ESP32 - RS485 Modul

  RX    GPIO 14 - RS485 RO
  TX    GPIO 13 - RS485 DI
  DE_RE GPIO  1 - RS485 DE/RE
  -----------------------------
  Wiring: RS485 - RJ45

  A - Pin 2 (Orange)
  B - Pin 1 (White/Orange)
  -----------------------------

  Requestframe WattCycle BMS RS485-Mode(PY/GosPow1) = "~20024642E00202FD33\r"
  
****************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#define RGB_LED_PIN 48
#define RGB_LED_COUNT  1
Adafruit_NeoPixel rgbled(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);


WiFiClient espClient;
PubSubClient client(espClient);

// wlan
const char *ssid = "Hier deine SSID";
const char *password = "Hier dein Wlanpasswort";

// mqtt
const char* mqtt_server = "192.168.178.10";
const int mqtt_port = 1889;

const char* topic_root = "wattcycle/battery";
char buffer_topic[128];
char buffer_payload[8];

// Pylontech Checksum
#define MAX_FRAME_LENGTH 256
char rxBuffer[MAX_FRAME_LENGTH];
int rxIndex = 0;
bool recording = false;

//RS485 control
#define BMS_SERIAL Serial2
#define rxPin    14  
#define txPin    13  
#define DE_RE_PIN   1
#define RS485_TRANSMITT  HIGH
#define RS485_RECIVE    LOW

// Requestframe 
char *REQUEST = "~20024642E00202FD33\r";


#define DEBUG_SERIAL Serial

// unmark for Serial Log
#define DEBUG

#ifdef DEBUG
  #define DBG_PRINT(x) DEBUG_SERIAL.print(x)
  #define DBG_PRINTLN(x) DEBUG_SERIAL.println(x)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
#endif

char dbgbuffer[128]; 
//**********************************************************************************************************
// Serial.printf workaround:
// Da Serial.printf("Zahl in Hex: %02X\n", intNr); mit define nicht funktioniert!
// sprintf(dbgbuffer,""Zahl in Hex: %02X\n", intNr);  // formatierten string nach dbgbuffer kopieren
// DBG_PRINT(dbgbuffer);                              // und dann ausgeben
//**********************************************************************************************************


// timer
unsigned long startTime = millis();

// wlan status
const char *str_status[]= {
  "WL_IDLE_STATUS",
  "WL_NO_SSID_AVAIL",
  "WL_SCAN_COMPLETED",
  "WL_CONNECTED",
  "WL_CONNECT_FAILED",
  "WL_CONNECTION_LOST",
  "WL_DISCONNECTED"
};

// WiFi mode
const char *str_mode[]= { "WIFI_OFF", "WIFI_STA", "WIFI_AP", "WIFI_AP_STA" };

// WiFi handling
void connectWifi() {
  DBG_PRINT("Connecting as wifi client to SSID: ");
  DBG_PRINTLN(ssid);

  WiFi.disconnect();
 
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
  }

  WiFi.begin (ssid, password );

  //DBG_PRINTLN(WiFi.printDiag());

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    DBG_PRINT(".");
  }
  DBG_PRINTLN("");
  
  // Check connection
  if (WiFi.status() == WL_CONNECTED) {
    DBG_PRINT("WiFi connected; IP address: ");
    DBG_PRINTLN(WiFi.localIP());
  } else {
    DBG_PRINT("WiFi connect failed to ssid: ");
    DBG_PRINTLN(ssid);
    DBG_PRINT("WiFi password <");
    DBG_PRINT(password);
    DBG_PRINTLN(">");
    DBG_PRINTLN("Check for wrong typing!");
  }
} 

void signalError() {  // RGBLED blinking in case of error
  while(1) {
      rgbled.setPixelColor(0, rgbled.Color(255, 0, 0)); // Red
      rgbled.show();
      delay(500); // ms
      
      rgbled.setPixelColor(0, rgbled.Color(0, 0, 0)); // Off
      rgbled.show();
      delay(500); // ms
  }
}


void sendBmsRequest(char *msg) {

  digitalWrite(DE_RE_PIN, RS485_TRANSMITT);
    delayMicroseconds(100);
    BMS_SERIAL.write(msg);
    BMS_SERIAL.flush();
    delayMicroseconds(100);
    digitalWrite(DE_RE_PIN, RS485_RECIVE);

    DBG_PRINTLN("Send bms request");
}

// Funktion zur Berechnung der Pylontech-Checksumme
uint16_t calculatePylontechChecksum(const char* data) {
    uint32_t sum = 0;
    size_t length = strlen(data);
    
    for (size_t i = 1; i < length - 5; i++) {
        sum += (uint8_t)data[i];
    }
   
    uint16_t checksum = (~sum + 1) & 0xFFFF;

    sprintf(dbgbuffer,"Checksumme: %04X\n", checksum);
    DBG_PRINT(dbgbuffer);
  
    return checksum;
}


// Hilfsfunktion: Wandelt x Hex-Zeichen ab einer bestimmten Position in eine Zahl um
uint32_t parseHex(const char* str, int pos, int length) {
  uint32_t val = 0;
  for (int i = 0; i < length; i++) {
    char c = str[pos + i];
    val <<= 4;
    if (c >= '0' && c <= '9') val += (c - '0');
    else if (c >= 'A' && c <= 'F') val += (c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') val += (c - 'a' + 10);
  }
  return val;
}


void parsePylontechFrame(const char* frame) {
  
  uint16_t minCellmV = 9999; // Startwert sehr hoch ansetzen
  uint16_t maxCellmV = 0;
  uint8_t minCellNum = 0;
  uint8_t maxCellNum = 0;

  // 1. Validierung (Startzeichen prüfen)
  if (frame[0] != '~') return;

  // 2. Header parsen
  uint8_t version = parseHex(frame, 1, 2);
  uint8_t address = parseHex(frame, 3, 2);
  uint8_t cid1    = parseHex(frame, 5, 2);
  uint8_t cid2    = parseHex(frame, 7, 2);
  
 

  DBG_PRINTLN("=== PYLONTECH FRAME EMPFANGEN ===");
  sprintf(dbgbuffer,"Protokoll-Version: %02X\n", version);
  DBG_PRINT(dbgbuffer);

  sprintf(dbgbuffer,"Geräte-Adresse:    %d\n", address);
  DBG_PRINT(dbgbuffer);

  sprintf(dbgbuffer,"CID1 (Akkutyp):    %02X\n", cid1);
  DBG_PRINT(dbgbuffer);

  sprintf(dbgbuffer,"CID2 (Status):     %02X\n", cid2);
  DBG_PRINT(dbgbuffer);

      
  if (cid2 != 0) {
    DBG_PRINTLN("Warnung: BMS meldet einen Fehlercode!");
  }

  // 3. Zellenzahl
  int index = 17;

  // Zellspannungen
  uint8_t cellCount = parseHex(frame, index, 2);
  sprintf(dbgbuffer,"Anzahl Zellen:     %d\n", cellCount);
  DBG_PRINT(dbgbuffer);
  index += 2;

    
  DBG_PRINTLN("\n--- Einzelzellen ---");
  for (int i = 0; i < cellCount; i++) {
    uint16_t cellMv = parseHex(frame, index, 4);
    index += 4;

    sprintf(dbgbuffer,"Zelle %02d: %d mV\n", i + 1, cellMv);
    DBG_PRINT(dbgbuffer);
    
    //send mqtt data
    sprintf(buffer_topic,"%s%s_%02d", topic_root, "/Einzelzellen/Zelle", i+1);
    sprintf(buffer_payload,"%d", cellMv);
    client.publish(buffer_topic, buffer_payload);

    // Minimale Zellenspannung + Zelle ermitteln
    if (cellMv < minCellmV) {
      minCellmV = cellMv;
      minCellNum = i + 1; 
    }
    
    // Maximale Zellenspannung + Zelle ermitteln
    if (cellMv > maxCellmV) {
      maxCellmV = cellMv;
      maxCellNum = i + 1; 
    }
    
  }

  // Berechnung des Zell-Deltas (Spannungsunterschied)
  uint16_t deltaMv = maxCellmV - minCellmV;

  // Ausgabe
  DBG_PRINTLN("\n--- Zell-Statistiken ---");

  //DBG_PRINTF("Minimale Zelle: Zelle %02d (%d mV)\n", minCellNum, minCellmV);
  sprintf(dbgbuffer,"Minimale Zelle: Zelle %02d (%d mV)\n", minCellNum, minCellmV);
  DBG_PRINT(dbgbuffer);

  //DBG_PRINTF("Maximale Zelle: Zelle %02d (%d mV)\n", maxCellNum, maxCellmV );
  sprintf(dbgbuffer,"Maximale Zelle: Zelle %02d (%d mV)\n", maxCellNum, maxCellmV);
  DBG_PRINT(dbgbuffer);

  //DBG_PRINTF("Zell-Differenz (Delta): %d mV (%.3f V)\n", deltaMv, (float)deltaMv / 1000.0);
  sprintf(dbgbuffer,"Zell-Differenz (Delta): %d mV (%.3f V)\n", deltaMv, (float)deltaMv / 1000.0);
  DBG_PRINT(dbgbuffer);

  //send mqtt data
  sprintf(buffer_topic,"%s%s", topic_root, "/min_cell_voltage");
  sprintf(buffer_payload,"%d", minCellmV);
  client.publish(buffer_topic, buffer_payload);

  sprintf(buffer_topic,"%s%s", topic_root, "/cell_with_min_voltage");
  sprintf(buffer_payload,"C%d", minCellNum);
  client.publish(buffer_topic, buffer_payload);

  sprintf(buffer_topic,"%s%s", topic_root, "/max_cell_voltage");
  sprintf(buffer_payload,"%d", maxCellmV);
  client.publish(buffer_topic, buffer_payload);

  sprintf(buffer_topic,"%s%s", topic_root, "/cell_with_max_voltage");
  sprintf(buffer_payload,"C%d", maxCellNum);
  client.publish(buffer_topic, buffer_payload);

  sprintf(buffer_topic,"%s%s", topic_root, "/cell_difference");
  sprintf(buffer_payload,"%d", deltaMv);
  client.publish(buffer_topic, buffer_payload);


  // Temperaturen
  uint8_t tempCount = parseHex(frame, index, 2);
  index += 2;

  DBG_PRINTLN("\n--- Temperaturen ---");
  for (int i = 0; i < tempCount; i++) {
    uint16_t tempKelvin10 = parseHex(frame, index, 4);
    index += 4;
    float tempC = ((float)tempKelvin10 / 10.0) - 273.15;
    //DBG_PRINTF("Sensor %d: %.1f °C\n", i + 1, tempC);
    sprintf(dbgbuffer,"Sensor %d: %.1f °C\n", i + 1, tempC);
    DBG_PRINT(dbgbuffer);
    
    //send mqtt data
    sprintf(buffer_topic,"%s%s_%02d", topic_root, "/Temperaturen/Sensor", i+1);
    sprintf(buffer_payload,"%.1f", tempC);
    client.publish(buffer_topic, buffer_payload);
  }

  // 4. Systemwerte
  // Strom (Signed 16-Bit für Laden/Entladen)
  int16_t currentRaw = (int16_t)parseHex(frame, index, 4);
  index += 4;
  float currentA = (float)currentRaw / 100.0;
  //send mqtt data
  sprintf(buffer_topic,"%s%s", topic_root, "/current");
  sprintf(buffer_payload,"%.1f", currentA);
  client.publish(buffer_topic, buffer_payload); 

  // Gesamtspannung
  uint16_t totalVoltageMv = parseHex(frame, index, 4);
  index += 4;
  float totalVoltageV = (float)totalVoltageMv / 1000.0;
  //send mqtt data
  sprintf(buffer_topic,"%s%s", topic_root, "/voltage");
  sprintf(buffer_payload,"%.1f", totalVoltageV);
  client.publish(buffer_topic, buffer_payload);

  // Verbleibende Kapazität
  uint16_t remainCapacityMha = parseHex(frame, index, 4);
  index += 4;
  float remainCapacityAh = (float)remainCapacityMha / 1000.0;

  // Überspringe User-ID (2 Stellen) & Totale Kapazität Platzhalter (4 Stellen)
  index += 6;

  // Zyklenanzahl
  uint16_t cycles = parseHex(frame, index, 4);
  //send mqtt data
  sprintf(buffer_topic,"%s%s", topic_root, "/charge_cycles");
  sprintf(buffer_payload,"%d", cycles);
  client.publish(buffer_topic, buffer_payload);

  
  DBG_PRINTLN("\n--- Systemwerte ---");
  //DBG_PRINTF("Gesamtspannung:    %.2f V\n", totalVoltageV);
  sprintf(dbgbuffer,"Gesamtspannung:    %.2f V\n", totalVoltageV);
  DBG_PRINT(dbgbuffer);

  //DBG_PRINTF("Stromfluss:        %.2f A\n", currentA);
  sprintf(dbgbuffer,"Stromfluss:        %.2f A\n", currentA);
  DBG_PRINT(dbgbuffer);

  //DBG_PRINTF("Restkapazität:     %.2f Ah\n", remainCapacityAh);
  sprintf(dbgbuffer,"Restkapazität:     %.2f Ah\n", remainCapacityAh);
  DBG_PRINT(dbgbuffer);

  //DBG_PRINTF("Ladezyklen:        %d\n", cycles);
  sprintf(dbgbuffer,"Ladezyklen:        %d\n", cycles);
  DBG_PRINT(dbgbuffer);

  DBG_PRINTLN("=================================\n");

  
}


//mqtt reconnent
void reconnect() {
  while (!client.connected()) {
    DBG_PRINTLN("Versuche MQTT-Verbindung...");
    // Einzigartige Client-ID erzeugen
    String clientId = "ESP32S3Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      DBG_PRINTLN("verbunden!");
      client.publish("esp32s3/status", "online");
    } else {
      DBG_PRINT("Fehlgeschlagen, RC=");
      DBG_PRINT(client.state());
      DBG_PRINTLN(" - Nächster Versuch in 5 Sekunden");
      delay(5000);
    }
  }
}


void setup() {

  // turn off RGB LED
  rgbled.begin();
  rgbled.clear();
    
  // Setup Serial
  Serial.begin(115200);
  rgbled.setPixelColor(0, rgbled.Color(0, 0, 255)); // Blue
  rgbled.show();
  delay(1000);
  rgbled.setPixelColor(0, rgbled.Color(0, 0, 0)); // off
  rgbled.show();
   
  // setup RS485 (Serial2)
  BMS_SERIAL.begin(9600, SERIAL_8N1, rxPin, txPin);
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, RS485_RECIVE); 

  
  DBG_PRINT("Chip ID: 0x");
  DBG_PRINTLN(ESP.getChipModel());

  DBG_PRINTLN( "Connect to Router requested" );

  connectWifi();
  if (WiFi.status() == WL_CONNECTED) {
    DBG_PRINT("WiFi mode: ");
    DBG_PRINTLN(str_mode[WiFi.getMode()]);
    DBG_PRINT( "Status: " );
    DBG_PRINTLN(str_status[WiFi.status()]);
    // signal WiFi connect
    rgbled.setPixelColor(0, rgbled.Color(0, 255, 0)); // Green
    rgbled.show();
    delay(500); // ms
    rgbled.setPixelColor(0, rgbled.Color(0, 0, 0)); // off
    rgbled.show();      
  } else {
    DBG_PRINTLN("");
    DBG_PRINTLN("WiFi connect failed, push RESET button.");
    signalError();
  }

  client.setServer(mqtt_server, mqtt_port); 

} 


void loop() {
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Hält die Verbindung am Leben


  // send bms request 
  if (millis() - startTime > 10000) { // run every 2000 ms
    startTime = millis();
    sendBmsRequest(REQUEST);  
  }

 
  while (BMS_SERIAL.available()) {
    char c = BMS_SERIAL.read();

    // Startzeichen erkannt, Puffer zurücksetzen und Datenframe erfassen
    if (c == '~') {
      DBG_PRINTLN("Startzeichen erkannt: ~");
      rxIndex = 0;
      recording = true;
    }
    
    if (recording) {
      if (rxIndex < MAX_FRAME_LENGTH -  1) {
        rxBuffer[rxIndex++] = c;
        rxBuffer[rxIndex] = '\0';
      }

      // Ende der Nachricht (Carriage Return oder Line Feed)
      if (c == '\r' || c == '\n') {
        DBG_PRINTLN("Ende der Nachricht erkannt");
        recording = false;
        // Nur parsen, wenn der Frame eine Mindestlänge hat
        if (rxIndex > 20) {
          calculatePylontechChecksum(rxBuffer);
          parsePylontechFrame(rxBuffer);
        }
        rxIndex = 0;
      }
    }     
  }
  
 // loop ende  
}
