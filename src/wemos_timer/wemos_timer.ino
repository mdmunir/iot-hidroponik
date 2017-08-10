#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define EEPROM_VALID B10101010
#define SSID_ADDR 0
#define PASS_ADDR 100

char ssid[90] = "cakmunir";
char password[90] = "cakmunir";

boolean newSSID, newPASS;

ESP8266WebServer server(80);

void handleRoot() {
  server.send (200, "text/plain", "Hidroponik system");
}

void handleSerial() {
  String cmd = server.arg("cmd");
  Serial.print(cmd);
  String res = Serial.readString();
  server.send(200, "text/plain", res);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "  /serial?cmd=command\n";
  message += "  /analog\n";

  server.send (404, "text/plain", message);
}

void handleReadAnalog() {
  int a = analogRead(A0);
  server.send(200, "text/plain", String(a));
}

void setup ( void ) {
  Serial.begin(115200);

  // get ssid dan password from EEPROM
  if (EEPROM.read(SSID_ADDR) == EEPROM_VALID) { // eeprom valid
    EEPROM.get(SSID_ADDR + 1, ssid);
  }
  if (EEPROM.read(PASS_ADDR) == EEPROM_VALID) {
    EEPROM.get(PASS_ADDR, password);
  }

  while ( WiFi.status() != WL_CONNECTED ) {
    WiFi.begin(ssid, password);
    newSSID = false;
    newPASS = false;
    while ( WiFi.status() != WL_CONNECTED ) {
      delay (1000);
      // kali aja ada inputan dari serial :D
      processSerial();
      if (newSSID && newPASS) {
        break;
      }
    }
  }

  if (MDNS.begin("esp8266")) {
    // do something
  }

  server.on("/", handleRoot );
  server.on("/serial", handleSerial);
  server.on("/analog", handleReadAnalog);

  server.onNotFound(handleNotFound);
  server.begin();
}

void processSerial() {
  int i;
  char command = Serial.read();
  switch (command) {
    case 'S': // SSID
    case 's': // tanpa simpan ke eeprom
      if (command == 'S' && EEPROM.read(SSID_ADDR) != EEPROM_VALID) {
        EEPROM.write(SSID_ADDR, EEPROM_VALID);
      }
      i = Serial.readBytes(ssid, 90);
      ssid[i] = '\0';
      newSSID = true;
      if (command == 'S') {
        EEPROM.put(SSID_ADDR + 1, ssid);
      }
      break;

    case 'P': // password
    case 'p': // tanpa simpan
      if (command == 'P' && EEPROM.read(PASS_ADDR) != EEPROM_VALID) {
        EEPROM.write(PASS_ADDR, EEPROM_VALID);
      }
      i = Serial.readBytes(password, 90);
      password[i] = '\0';
      newPASS = true;
      if (command == 'P') {
        EEPROM.put(PASS_ADDR + 1, password);
      }
      break;

    default:
      break;
  }
}

void loop ( void ) {
  server.handleClient();

  if (Serial.available()) {
    processSerial();
  }
}

