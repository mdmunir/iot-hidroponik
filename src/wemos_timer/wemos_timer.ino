#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define EEPROM_VALID B10101010
#define SSID_ADDR 0 // alamat eeprom untuk ssid
#define PASS_ADDR 50

char ssid[40];
char password[40];
byte outputPins[] = {D0, D1, D2, D3, D4, D5, D6, D7};

boolean ssidPassAda = false;

ESP8266WebServer server(80);

String msgInfo = "  /serial?cmd=command\n  /analog\n  /pinout?cmd=action\n  /statepin";
void handleRoot() {
  String message = "Hidroponik system by Cak Munir\n\n";

  server.send (200, "text/plain", message + msgInfo);
}

void handleNotFound() {
  String message = "File Not Found\n\n";

  server.send (404, "text/plain", message + msgInfo);
}

void handleSerial() {
  String cmd = server.arg("cmd");
  Serial.print(cmd);
  String res = Serial.readString();
  server.send(200, "text/plain", res);
}

void handleReadAnalog() {
  int a = analogRead(A0);
  server.send(200, "text/plain", String(a));
}

void handlePinout() {
  String cmd = server.arg("cmd");
  char action = cmd.charAt(0), pin = cmd.charAt(1);
  byte x, state;
  if (pin >= '0' && pin <= '7') {
    x = outputPins[pin - '0'];
    switch (action) {
      case '1':
      case '0':
      case 'x':
        if (action == '1') {
          digitalWrite(x, HIGH);
        } else if (action == '0') {
          digitalWrite(x, LOW);
        } else {
          digitalWrite(x, !digitalRead(x));
        }
        server.send(200, "text/plain", "OK");
        break;
      case 'r':
        server.send(200, "text/plain", digitalRead(x) ? "ON" : "OFF");
        break;
      default:
        server.send(500, "text/plain", "Invalid command");
    }
  } else {
    server.send(500, "text/plain", "Invalid pin");
  }
}

void handleGetState() {
  String msg = "State pin:";
  byte i;
  for (i = 0; i < sizeof(outputPins); i++) {
    msg += (digitalRead(outputPins[i]) ? "\nON" : "\nOFF");
  }
  server.send(200, "text/plain", msg);
}

void processSerial() {
  if (!Serial.available()) {
    return;
  }
  int i;
  char command = Serial.read(), c;
  byte x;
  switch (command) {
    case 'S': // SSID
    case 's': // tanpa simpan ke eeprom
      if (command == 'S' && EEPROM.read(SSID_ADDR) != EEPROM_VALID) {
        EEPROM.write(SSID_ADDR, EEPROM_VALID);
      }
      i = Serial.readBytes(ssid, 40);
      ssid[i] = '\0';
      ssidPassAda = true;
      if (command == 'S') {
        EEPROM.put(SSID_ADDR + 1, ssid);
      }
      break;

    case 'P': // password
    case 'p': // tanpa simpan
      if (command == 'P' && EEPROM.read(PASS_ADDR) != EEPROM_VALID) {
        EEPROM.write(PASS_ADDR, EEPROM_VALID);
      }
      i = Serial.readBytes(password, 40);
      password[i] = '\0';
      ssidPassAda = true;
      if (command == 'P') {
        EEPROM.put(PASS_ADDR + 1, password);
      }
      break;

    case '1':
    case '0':
    case 'x':
      c = Serial.read();
      if (c >= '0' && c <= '7') {
        x = c - '0';
        if (command == '1') {
          digitalWrite(outputPins[x], HIGH);
        } else if (command == '0') {
          digitalWrite(outputPins[x], LOW);
        } else {
          digitalWrite(outputPins[x], !digitalRead(outputPins[x]));
        }
      }
      break;

    default:
      break;
  }
}

/**
   Setup
*/
void setup(void) {
  int i;
  Serial.begin(115200);

  // set mode output
  for (i = 0; i < sizeof(outputPins); i++) {
    pinMode(outputPins[i], OUTPUT);
  }

  // get ssid dan password from EEPROM
  if (EEPROM.read(SSID_ADDR) == EEPROM_VALID) { // eeprom valid
    EEPROM.get(SSID_ADDR + 1, ssid);
    ssidPassAda = true;
  }
  if (EEPROM.read(PASS_ADDR) == EEPROM_VALID) {
    EEPROM.get(PASS_ADDR, password);
  } else {
    ssidPassAda = false;
  }

  while ( WiFi.status() != WL_CONNECTED ) {
    if (ssidPassAda) {
      WiFi.begin(ssid, password);
    }
    ssidPassAda = false;
    while ( WiFi.status() != WL_CONNECTED ) {
      delay (1000);
      // kali aja ada inputan dari serial :D
      processSerial();
      if (ssidPassAda) {
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
  server.on("/pinout", handlePinout);
  server.on("/statepin", handleGetState);

  server.onNotFound(handleNotFound);
  server.begin();
}

/**
   Main process
*/
void loop ( void ) {
  server.handleClient();
  processSerial();
}

