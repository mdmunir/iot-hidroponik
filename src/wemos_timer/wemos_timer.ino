#include <RealTimeClockDS1307.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define TIMER_VALID B1010
#define TDS_SOURCE_1 D3
#define TDS_SOURCE_2 D4

byte outputPins[] = {D5, D6, D7, D8};
int timerDurations[] = {0, 0, 0, 0};
char formatted[] = "00-00-00 00:00:00x";

struct TTimer {
  byte action;
  byte y;
  byte m;
  byte d;
  byte h;
  byte i;
  byte w;
  byte duration;
};

struct TTime {
  byte y;
  byte m;
  byte d;
  byte h;
  byte i;
  byte w;
};


ESP8266WebServer server(80);

const int TIMER_SIZE = sizeof(TTimer);
const int EEPROM_SIZE = 512;
const int TIMER_COUNT = EEPROM_SIZE / TIMER_SIZE;
const int PIN_COUNT = sizeof(outputPins);

long count = 0, m, lm = 0;
TTime currentTime;
/**
   Setup
*/
void setup(void) {
  int i;
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  // set mode output
  for (i = 0; i < PIN_COUNT; i++) {
    pinMode(outputPins[i], OUTPUT);
  }

  // set mode output untuk tds sensor
  pinMode(TDS_SOURCE_1, OUTPUT);
  digitalWrite(TDS_SOURCE_1, LOW);
  pinMode(TDS_SOURCE_2, OUTPUT);
  digitalWrite(TDS_SOURCE_2, LOW);

  i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 30) { // 30 detik
    processSerial();
    delay (1000);
    i++;
  }

  if (MDNS.begin("esp8266")) {
    // do something
  }

  server.on("/", handleRoot );
  server.on("/analog", handleReadAnalog);
  server.on("/state", handleState);
  server.on("/timer", handleTimer);

  server.onNotFound(handleNotFound);
  server.begin();

  currentTime.y = 255;
  currentTime.m = 255;
  currentTime.d = 255;
  currentTime.h = 0;
  currentTime.i = 0;
  currentTime.w = 255;
  kalibrasiTime();
}

/**
   Main process
*/
void loop ( void ) {
  byte x;
  server.handleClient();
  processSerial();
  m = millis() / 1000;
  if (m != lm) {
    lm = m;
    for (x = 0; x < PIN_COUNT; x++) {
      if (timerDurations[x] > 0) {
        timerDurations[x]--;
        if (timerDurations[x] == 0) {
          digitalWrite(outputPins[x], LOW);
        }
      }
    }
    if (m % 60 == 0) {
      processTimer();
    }
    count++;
  }
}


String msgInfo = "  /analog\n  /state?cmd=(1|0|x|r)&pin=no_pin\n  /timer?action=(a|e|d)&i=index&content=content";
void handleRoot() {
  String message = "Hidroponik system by Cak Munir\n\n";

  server.send (200, "text/plain", message + msgInfo);
}

void handleNotFound() {
  String message = "File Not Found\n\n";

  server.send (404, "text/plain", message + msgInfo);
}

void handleReadAnalog() {
  //int a = analogRead(A0);
  server.send(200, "text/plain", String(samplingTds()));
}

void handleState() {
  byte x;
  char action = server.hasArg("cmd") ? server.arg("cmd").charAt(0) : '\0';
  char pin  = server.hasArg("pin") ? server.arg("pin").charAt(0) : '\0';
  int duration = server.hasArg("duration") ? server.arg("duration").toInt() : 0;
  String msg = "";
  boolean success = false;

  switch (action) {
    case '1':
    case '0':
    case 'x':
      if (pin >= '0' && pin - '0' < PIN_COUNT) {
        x = outputPins[pin - '0'];
        setStatePin(action == '1' || (action == 'x' && !digitalRead(x)), pin - '0', duration);
        msg = digitalRead(x) ? "ON" : "OFF";
        success = true;
      } else {
        msg = "Invalid pin [" + server.arg("pin") + "]";
        success = false;
      }
      break;

    case 'r':
    case '\0':
      if (pin >= '0' && pin - '0' < PIN_COUNT) {
        x = outputPins[pin - '0'];
        msg = digitalRead(x) ? "ON" : "OFF";
      } else {
        msg = "";
        for (x = 0; x < PIN_COUNT; x++) {
          msg += (digitalRead(outputPins[x]) ? "1" : "0");
        }
      }
      success = true;
      break;
    default:
      success = false;
      msg = "Invalid command";
      break;
  }
  server.send(success ? 200 : 500, "text/plain", msg);
}

void handleTimer() {
  String s = server.arg("content");
  String msg;
  char c = server.hasArg("action") ? server.arg("action").charAt(0) : '\0';
  int idx = server.hasArg("i") ? server.arg("i").toInt() : -1;
  boolean success;
  switch (c) {
    case 'a':
    case 'e':
      success = setTimer(s, c == 'a' ? -1 : idx);
      if (success) {
        msg = "Success add/edit timer [" + s + "]";
      } else {
        msg = "Fail add/edit timer [" + s + "]";
      }
      break;
    case 'd':
      success = deleteTimer(idx);
      if (success) {
        msg = "Success delete timer";
      } else {
        msg = "Fail delete timer";
      }
      break;
    case '\0': // show timer
      msg = showTimer(idx);
      success = true;
      break;
    default:
      msg = "Unknown action \"" + server.arg("action") + "\"";
      success = false;
      break;
  }
  server.send(success ? 200 : 500, "text/plain", msg);
}

void processSerial() {
  if (!Serial.available()) {
    return;
  }
  int i, duration;
  char c, command;
  byte x;
  String s = Serial.readString();
  char ssid[40], pass[40];
  float tds;

  command = s[0];
  switch (command) {
    case 's' :
      i = s.indexOf(':');
      if (i >= 0) {
        s.substring(1, i - 1).toCharArray(ssid, 40);
        s.substring(i + 1).toCharArray(pass, 40);
        if (WiFi.status() == WL_CONNECTED) {
          WiFi.disconnect();
        }
        WiFi.begin(ssid, pass);
        for (i = 0; i < 10; i++) { // tunggu 10 detik
          if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Connect to [");
            Serial.print(ssid);
            Serial.print("] Ip address: ");
            Serial.println(WiFi.localIP());
            break;
          }
          delay(1000);
        }
      }
      break;

    case '1':
    case '0':
    case 'x':
      c = s[1];
      if (c >= '0' && (c - '0') < PIN_COUNT) {
        x = c - '0';
        i = 3;
        if (s.length() > 3) {
          duration = getIntFromStr(s, i, s.length());
        } else {
          duration = 0;
        }
        setStatePin(command == '1' || (command == 'x' && !digitalRead(outputPins[x])), x, duration);
        Serial.print(x);
        Serial.println(digitalRead(outputPins[x]) ? " ON" : " OFF");
      } else {
        Serial.print("Invalid pin [");
        Serial.print(c);
        Serial.println("]");
      }
      break;

    case 'n':
      RTC.readClock();
      RTC.getFormatted(formatted);
      Serial.print(formatted);
      Serial.print(' ');
      Serial.print(RTC.getDayOfWeek());
      Serial.println();
      break;

    case 'i': // get ip address
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Connect to [");
        Serial.print(WiFi.SSID());
        Serial.print("] IP Address: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("Not connected");
      }
      break;

    case 't': // get tds
      tds = samplingTds();
      Serial.println(tds);
      break;

    case 'a':
      i = analogRead(A0);
      Serial.println(i);
      break;

    default:
      break;
  }
}


int getTimerAddress(int idx) {
  TTimer obj;
  int c = 0, x;
  for (x = 0; x < TIMER_COUNT; x++) {
    EEPROM.get(x * TIMER_SIZE, obj);
    if (obj.action >> 4 == TIMER_VALID) { // valid
      if (idx == c) {
        return x * TIMER_SIZE;
      }
      c++;
    } else if (idx < 0) {
      return x * TIMER_SIZE;
    }
  }
  return -1;
}

int getIntFromStr(String ss, int &i, int l) {
  int r = 0;
  boolean done = false;
  char c;
  while (!done && i < l) {
    c = ss[i];
    if (c >= '0' && c <= '9') {
      r = r * 10 + (c - '0');
      i++;
    } else {
      done = true;
    }
  }
  return r;
}

boolean setTimer(String ss, int idx) {
  int i, l = ss.length(), inp;
  idx = getTimerAddress(idx);
  TTimer obj;
  if (idx >= 0) {
    obj.action = TIMER_VALID << 4;
    obj.action += (ss[0] == '1' ? 1 << 3 : 0);
    obj.action += (ss[1] - '0');

    i = 3;
    // year
    if (ss[i] == '*') {
      obj.y = 255;
      i += 2;
    } else {
      inp = getIntFromStr(ss, i, l);
      i++;
      if (inp >= 0 && inp <= 99) {
        obj.y = inp;
      } else {
        return false;
      }
    }
    // month
    if (ss[i] == '*') {
      obj.m = 255;
      i += 2;
    } else {
      inp = getIntFromStr(ss, i, l);
      i++;
      if (inp >= 1 && inp <= 12) {
        obj.m = inp;
      } else {
        return false;
      }
    }
    // day
    if (ss[i] == '*') {
      obj.d = 255;
      i += 2;
    } else {
      inp = getIntFromStr(ss, i, l);
      i++;
      if (inp >= 1 && inp <= 31) {
        obj.d = inp;
      } else {
        return false;
      }
    }
    // hour
    if (ss[i] == '*') {
      obj.h = 255;
      i += 2;
    } else {
      inp = getIntFromStr(ss, i, l);
      i++;
      if (inp >= 0 && inp <= 23) {
        obj.h = inp;
      } else {
        return false;
      }
    }
    // minute
    if (ss[i] == '*') {
      obj.i = 255;
      i += 2;
    } else {
      inp = getIntFromStr(ss, i, l);
      i++;
      if (inp >= 0 && inp <= 59) {
        obj.i = inp;
      } else {
        return false;
      }
    }
    // day of week
    if (ss[i] == '*') {
      obj.w = 255;
    } else if (ss[i] >= '1' && ss[i] <= '7') {
      obj.w = (ss[i] - '0');
    } else {
      return false;
    }
    i += 2;

    // duration
    if (i < l - 1) {
      inp = getIntFromStr(ss, i, l);
      if (inp > 0 && inp <= 255) {
        obj.duration = inp;
      } else {
        obj.duration = 0;
      }
    }

    // save to eeprom
    EEPROM.put(idx, obj);
    EEPROM.commit();
    return true;
  }
  return false;
}

boolean deleteTimer(int idx) {
  idx = getTimerAddress(idx);
  TTimer obj;
  if (idx >= 0) {
    obj.action = 0;
    EEPROM.put(idx, obj);
    EEPROM.commit();
    return true;
  }
  return false;
}

String _formatTimer(TTimer obj) {
  String r = "";
  r += (obj.action & B1000) ? "ON " : "OFF "; // action
  r += String(obj.action % 8); // pin
  r += " ";
  r += (obj.y == 255) ? "*" : String(obj.y); // year
  r += "-";
  r += (obj.m == 255) ? "*" : String(obj.m); // month
  r += "-";
  r += (obj.d == 255) ? "*" : String(obj.d); // year
  r += " ";
  r += (obj.h == 255) ? "*" : String(obj.h); // hour
  r += ":";
  r += (obj.i == 255) ? "*" : String(obj.i); // minute
  r += " ";
  r += (obj.w == 255) ? "*" : String(obj.w); // day of week

  if (obj.duration > 0) {
    r += " " + String(obj.duration);
  }
  return r;
}

String showTimer(int idx) {
  String s = "";
  TTimer obj;
  int c = 0, x;
  for (x = 0; x < TIMER_COUNT; x++) {
    EEPROM.get(x * TIMER_SIZE, obj);
    if (obj.action >> 4 == TIMER_VALID) { // valid
      if (idx == c) {
        return _formatTimer(obj);
      } else if (idx < 0) {
        s += _formatTimer(obj) + "\n";
      }
      c++;
    }
  }
  s += "Available: " + String(TIMER_COUNT - c);
  return s;
}


void processTimer() {
  TTimer obj;
  int i;
  byte x;
  boolean match, action;
  kalibrasiTime();

  for (i = 0; i < TIMER_COUNT; i++) {
    EEPROM.get(i * TIMER_SIZE, obj);
    if (obj.action >> 4 == TIMER_VALID) { // valid
      match = (obj.y == 255 || obj.y == currentTime.y) &&
              (obj.m == 255 || obj.m == currentTime.m) &&
              (obj.d == 255 || obj.d == currentTime.d) &&
              (obj.h == 255 || obj.h == currentTime.h) &&
              (obj.i == 255 || obj.i == currentTime.i) &&
              (obj.w == 255 || obj.w == currentTime.w);

      if (match) {
        x = obj.action % 8;
        action = (obj.action & B1000) == B1000;
        setStatePin(action, x, obj.duration);
      }
    }
  }
}

void setStatePin(boolean action, byte x, int duration) {
  digitalWrite(outputPins[x], action);
  if (action) {
    timerDurations[x] = 60 * duration;
  } else {
    timerDurations[x] = 0;
  }
}

void kalibrasiTime() {
  RTC.readClock();
  if (RTC.getYear() < 99) { // RTC valid
    currentTime.y = RTC.getYear();
    currentTime.m = RTC.getMonth();
    currentTime.d = RTC.getDate();
    currentTime.h = RTC.getHours();
    currentTime.i = RTC.getMinutes();
    currentTime.w = RTC.getDayOfWeek();
  } else {
    currentTime.i++;
    if (currentTime.i > 59) {
      currentTime.i = 0;
      currentTime.h++;
      if (currentTime.h > 23) {
        currentTime.h = 0;
      }
    }
  }
}

float samplingTds() {
  float x1, x2, r;
  float C1 = 0.0, C2 = 1000.0;
  // sampling 1 -> 1+, 2-
  digitalWrite(TDS_SOURCE_1, HIGH);
  digitalWrite(TDS_SOURCE_2, LOW);
  x1 = analogRead(A0);

  // sampling 2 -> 1-, 2+
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, HIGH);
  x2 = analogRead(A0);

  // idle
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  r = (x1 + 1) / (1024 - x1) + (1024 - x2) / (x2 + 1);
  return C1 + C2 / r;
}



