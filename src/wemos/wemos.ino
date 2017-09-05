/**
Serial command adalah perintah untuk wemos yang dikirimkan dari serial port (serial monitor). Berikut beberapa perintah yang tersedia
  - ssid:<SSID>:<PASSWORD>  => Digunakan untuk mengganti koneksi wifi. Contoh ssid:cakmunir:rahasia
  - ip => Mendapatkan IP address dari wemos
  - tds => Membaca nilai kepekatan larutan
  - now => Membaca waktu sekarang dari RTC
  - set_time:<FORMAT> => Menset waktu dari RTC module. Format yang dipakai adalah "YY-MM-DD HH:ii:SS [W]". Contoh set_time:17-09-05 11:34:26 2
  - konfig:<VALUES> => Menset nilai dari beberapa pengaturan antara lain ppm, periode pompa, durasi nyala pompa. Contoh
                       konfig:p1200;t30;d5 -> menset kepekatan larutan menadi 1200 ppm, periode nyala pompa 30 menit serta durasi pompa 5 menit.
*/

#include <RealTimeClockDS1307.h>
#include <SD.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define EEPROM_VALID B1001
#define TDS_SOURCE_1 SDA
#define TDS_SOURCE_2 SCL
#define POMPA D3
#define VALVE D4
#define T_POMPA 0
#define T_VALVE 1
#define EEPROM_SIZE 64
#define CS D8


/* *************************************************** */
//              CLASS PROCESS SERIAL
/* *************************************************** */
class TSerialCommand {
  private:
    String _cmd;
    void (*_callback)(String);
    TSerialCommand* _next;

  public:
    TSerialCommand(String cmd, void (*callback)(String)) {
      _cmd = cmd;
      _callback = callback;
    }
    TSerialCommand* next() {
      return _next;
    }
    void next(TSerialCommand* next) {
      _next = next;
    }
    boolean process(String cmd, String content) {
      if (_cmd == cmd) {
        _callback(content);
        return true;
      }
      return false;
    }
};


class TSerialProcess {
  private:
    TSerialCommand* _first;
    TSerialCommand* _last;
    String _cmd;
    String _param;
    void (*_customProcess)(String);

  public:
    String cmd() {
      return _cmd;
    }
    String param() {
      return _param;
    }
    void setCustomProcess(void (*callback)(String)) {
      _customProcess = callback;
    }

    void addCommand(String cmd, void (*callback)(String)) {
      TSerialCommand *sc = new TSerialCommand(cmd, callback);
      if (!_last) {
        _first = sc;
        _last = sc;
      } else {
        _last->next(sc);
        _last = sc;
      }
    }

    void processCommand() {
      if (!Serial.available()) {
        return;
      }
      String s = Serial.readString();
      int i = s.indexOf(':');
      TSerialCommand *sc = _first;
      if (i > 0) {
        _cmd = s.substring(0, i);
        _param = s.substring(i + 1);
      } else {
        _cmd = s;
        _param = "";
      }
      while (sc) {
        if (sc->process(_cmd, _param)) {
          return;
        } else {
          sc = sc->next();
        }
      }

      if (_customProcess) {
        _customProcess(s);
      } else {
        Serial.print("Unknown command: ");
        Serial.println(s);
      }
    }
};



/* *************************************************** */
//              GLOBAL VAR
/* *************************************************** */

char formatted[] = "00-00-00 00:00:00x";

struct TConfig {
  byte check;
  int ppm;
  int periode;// periode pompa dalam menit
  byte start_at; // menit
  byte duration;
};

struct {
  byte y;
  byte m;
  byte d;
  byte h;
  byte i;
  byte s;
  byte w;
  long last;
} currentTime;


ESP8266WebServer server(80);
TSerialProcess serialProcess;

long count = 0, seconds, lastSeconds = 0;
long durations[] = {0, 0}; // 0 -> pompa, 1 -> valve
int lastAddedNutrision;
boolean rtcValid = false;
TConfig konfig;

/* ******************************************************* */
//                     MAIN FUNCTION
/* ******************************************************* */
void setup(void) {
  int i;
  unsigned int ppm;

  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  // set mode output untuk tds sensor
  pinMode(POMPA, OUTPUT);
  pinMode(VALVE, OUTPUT);

  serialProcess.addCommand("ssid", setSsidPass);
  serialProcess.addCommand("ip", getIp);
  serialProcess.addCommand("tds", samplingTds);
  serialProcess.addCommand("now", getTimeNow);
  serialProcess.addCommand("set_time", setTimeNow);
  serialProcess.addCommand("pins", getPinStates);
  serialProcess.addCommand("test", testTds);
  serialProcess.addCommand("konfig", setKonfig);
  serialProcess.addCommand("analog", readAnalog);

  i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 30) { // 30 detik
    serialProcess.processCommand();
    delay (1000);
    i++;
  }

  if (MDNS.begin("esp8266")) {
    // do something
  }

  server.on("/", handleRoot );
  server.on("/tds", handleReadTds);
  server.on("/pompa", handlePompa);
  server.on("/config", handleConfig);

  server.onNotFound(handleNotFound);
  server.begin();

  // current time
  currentTime.y = 255;
  currentTime.m = 255;
  currentTime.d = 255;
  currentTime.h = 0;
  currentTime.i = 0;
  currentTime.s = 0;
  currentTime.w = 255;
  currentTime.last = millis() / 1000;
  getCurrentTime();

  // read config
  TConfig _config;
  EEPROM.get(0, _config);
  if (_config.check == EEPROM_VALID) {
    konfig.ppm = _config.ppm;
    konfig.periode = _config.periode;
    konfig.start_at = _config.start_at;
    konfig.duration = _config.duration;
  } else {
    // nilai default
    konfig.check = EEPROM_VALID;
    konfig.ppm = 1000;
    konfig.periode = 60; // tiap jam
    konfig.start_at = 0;
    konfig.duration = 5; // 5 menit
  }
  if (SD.begin(CS)) {
    // create dir
    if (!SD.exists("log")) {
      SD.mkdir("log");
    }
  }
}

/**
   Main process
*/
void loop ( void ) {
  byte x;
  int mnt, xmnt;
  String txt;
  server.handleClient();
  serialProcess.processCommand();

  seconds = millis() / 1000;
  if (seconds != lastSeconds) { // ganti detik
    lastSeconds = seconds;
    getCurrentTime();

    // matikan pompa
    if (durations[T_POMPA] > 0) {
      durations[T_POMPA]--;
      if (durations[T_POMPA] == 0) {
        digitalWrite(POMPA, LOW);
      }
    }
    // matikan valve
    if (durations[T_VALVE] > 0) {
      durations[T_VALVE]--;
      if (durations[T_VALVE] == 0) {
        digitalWrite(VALVE, LOW);
        txt = "tambahan nutrisi = " + String(lastAddedNutrision);
        addLogText(txt);
      }
    }

    if (seconds % 60 == 0) { // ganti menit
      mnt = currentTime.h * 60 + currentTime.i;
      xmnt = mnt % konfig.periode;

      // SCHEDULER POMPA dan VALVE
      if (xmnt == konfig.start_at) {
        // log ppm sebelum pemberian nutrisi
        txt = "ppm = " + String(getTds());
        addLogText(txt);
        nutrisiOtomatis();
        setPompa(true, konfig.duration * 60);
      }
    }
    count++;
  }
}


/* ******************************************************* */
//                     WIFI SERVICE FUNCTION
/* ******************************************************* */

String msgInfo = "  /tds\n  /pompa?cmd=(1|0|x|r)&duration=duration\n  /config?ppm=ppm&periode=periode&start_at=start_at&duration=duration";
void handleRoot() {
  String message = "Hidroponik system by Cak Munir\n\n";

  server.send (200, "text/plain", message + msgInfo);
}

void handleNotFound() {
  String message = "File Not Found\n\n";

  server.send (404, "text/plain", message + msgInfo);
}

void handleReadTds() {
  server.send(200, "text/plain", String(getTds()));
}

void handleConfig() {
  int ppm, periode, start_at, duration;
  char temp[100];
  boolean safe = false;
  if (server.hasArg("ppm")) {
    ppm = server.arg("ppm").toInt();
    if (ppm >= 500 && ppm <= 2500) {
      konfig.ppm = ppm;
      safe = true;
    }
  }
  if (server.hasArg("periode")) {
    periode = server.arg("periode").toInt();
    if (periode >= 15 && periode <= 720) { // 15 menit sampai 12 jam
      konfig.periode = periode;
      safe = true;
    }
  }
  if (server.hasArg("start_at")) {
    start_at = server.arg("start_at").toInt();
    if (start_at >= 0 && start_at < konfig.periode) {
      konfig.start_at = start_at;
      safe = true;
    }
  }
  if (server.hasArg("duration")) {
    duration = server.arg("duration").toInt();
    if (duration >= 0 && duration < 255) {
      konfig.duration = duration;
      safe = true;
    }
  }
  if (safe) {
    EEPROM.put(0, konfig);
    EEPROM.commit();
  }
  snprintf(temp, 100, "Konfig:\nppm = %d\nperiode = %d\nstart at = %d\nduration= %d", konfig.ppm, konfig.periode, konfig.start_at, konfig.duration);
  server.send(200, "text/plain", temp);
}

void handlePompa() {
  char action = server.hasArg("cmd") ? server.arg("cmd").charAt(0) : '\0';
  int duration = server.hasArg("duration") ? server.arg("duration").toInt() : 0;

  String msg = "";
  boolean state, success = false;

  switch (action) {
    case '1':
    case '0':
    case 'x':
      state = action == '1' || (action == 'x' && !digitalRead(POMPA));
      setPompa(state, duration * 60);
    case 'r':
    case '\0':
      msg = digitalRead(POMPA) ? "ON" : "OFF";
      success = true;
      break;
    default:
      success = false;
      msg = "Invalid command";
      break;
  }
  server.send(success ? 200 : 500, "text/plain", msg);
}


/* ******************************************************* */
//                     SERIAL FUNCTION
/* ******************************************************* */
void getTimeNow(String s) {
  RTC.readClock();
  RTC.getFormatted(formatted);
  Serial.print(formatted);
  Serial.print(' ');
  Serial.print(RTC.getDayOfWeek());
  Serial.println();
}

/*
   Set current time from serial
   YY-MM-DD HH:II:SS [W]
*/
void setTimeNow(String s) {
  int inp, i = 0;
  inp = getIntFromStr(s, i);
  RTC.setYear(inp);
  i++;
  inp = getIntFromStr(s, i);
  RTC.setMonth(inp);
  i++;
  inp = getIntFromStr(s, i);
  RTC.setDate(inp);
  i++;
  inp = getIntFromStr(s, i);
  RTC.setHours(inp);
  i++;
  inp = getIntFromStr(s, i);
  RTC.setMinutes(inp);
  i++;
  inp = getIntFromStr(s, i);
  RTC.setSeconds(inp);
  i++;
  if (i < s.length() - 1) {
    inp = getIntFromStr(s, i);
    RTC.setDayOfWeek(inp);
  }
  RTC.setClock();

  Serial.println("Done setting time");
}

void setSsidPass(String s) {
  char ssid[40], pass[40];
  int i = s.indexOf(':');
  if (i >= 0) {
    s.substring(0, i).toCharArray(ssid, 40);
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
}

void getIp(String s) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connect to [");
    Serial.print(WiFi.SSID());
    Serial.print("] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Not connected");
  }
}

void samplingTds(String s) {
  float x = getTds();
  Serial.println(x);
}

void setKonfig(String s) {
  int i, inp, p;
  char c;
  String s1, s2 = s;
  while (s2.length() > 0) {
    p = s2.indexOf(';');
    if (p > 0) {
      s1 = s2.substring(0, p);
      s2 = s2.substring(p + 1);
    } else {
      s1 = s2;
      s2 = "";
    }
    c = s1[0];
    i = 1;
    inp = getIntFromStr(s1, i);
    switch (c) {
      case 'p':
        if (inp >= 500 && inp <= 2500) {
          konfig.ppm = inp;
        }
        break;
      case 't':
        if (inp >= 15 && inp <= 720) {
          konfig.periode = inp;
        }
        break;
      case 's':
        if (inp >= 0 && inp < konfig.periode) {
          konfig.start_at = inp;
        }
        break;
      case 'd':
        if (inp >= 1 && inp <= 255) {
          konfig.duration = inp;
        }
        break;
    }
  }
  EEPROM.put(0, konfig);
  EEPROM.commit();
}

void getPinStates(String s) {
  byte pins[] = {D0, D1, D2, D3, D4, D5, D6, D7, D8};
  byte pins2[] = {RX, TX, SCL, SDA, MISO, MOSI, SS, SCK};
  String names[] = {"RX", "TX", "SCL", "SDA", "MISO", "MOSI", "SS", "SCK"};
  byte x;
  for (x = 0; x <= 8; x++) {
    Serial.print(pins[x]);
    Serial.print(" => D");
    Serial.print(x);
    Serial.print(" : ");
    Serial.println(digitalRead(pins[x]) ? '1' : '0');
  }
  for (x = 0; x < sizeof(pins2); x++) {
    Serial.print(pins2[x]);
    Serial.print(" => ");
    Serial.print(names[x]);
    Serial.print(" : ");
    Serial.println(digitalRead(pins2[x]) ? '1' : '0');
  }
}

void testTds(String s) {
  int a1, a2, i;

  // sampling 1 -> 1+, 2-
  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);

  for (i = 0; i < 5; i++) {
    digitalWrite(TDS_SOURCE_1, HIGH);
    digitalWrite(TDS_SOURCE_2, LOW);
    digitalWrite(TDS_SOURCE_1, LOW);
    digitalWrite(TDS_SOURCE_2, HIGH);
  }

  digitalWrite(TDS_SOURCE_1, HIGH);
  digitalWrite(TDS_SOURCE_2, LOW);
  a1 = analogRead(A0);
  Serial.print("A1 = ");
  Serial.print(a1);

  // sampling 2 -> 1-, 2+
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, HIGH);
  a2 = analogRead(A0);
  Serial.print(" | A2 = ");
  Serial.print(a2);

  // init twi
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  Serial.println();
}

void readAnalog(String s) {
  Serial.println(analogRead(A0));
}

/* ******************************************************* */
//                     OTHER FUNCTION
/* ******************************************************* */

int getIntFromStr(String ss, int &i) {
  int r = 0, l = ss.length();
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

void getCurrentTime() {
  long t = millis() / 1000, selisih = t - currentTime.last, s, i, h;
  byte months[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  byte lastDate;

  s = currentTime.s + selisih;
  i = currentTime.i + s / 60; // menit
  h = currentTime.h + i / 60; // jam

  RTC.readClock();
  if (RTC.getHours() < 24 && RTC.getMinutes() < 60 && RTC.getSeconds() < 60) {
    currentTime.h = RTC.getHours();
    currentTime.i = RTC.getMinutes();
    currentTime.s = RTC.getSeconds();
  } else {
    currentTime.s = s % 60;
    currentTime.i = i % 60;
    currentTime.h = h % 24;
  }

  if (RTC.getYear() < 99 && RTC.getMonth() <= 12 && RTC.getDate <= 31) { // RTC valid
    currentTime.y = RTC.getYear();
    currentTime.m = RTC.getMonth();
    currentTime.d = RTC.getDate();
    rtcValid = true;
  } else {
    if (h >= 24) { // ganti hari
      if (currentTime.m == 2 && currentTime.y % 4 == 0) { // kabisat
        lastDate = 29;
      } else {
        lastDate = months[currentTime.m];
      }
      if (currentTime.d < lastDate) {
        currentTime.d++;
      } else {
        currentTime.d = 1;
        if (currentTime.m == 12) {
          currentTime.m = 1;
          currentTime.y++;
        } else {
          currentTime.m++;
        }
      }
    }
  }

  if (RTC.getDayOfWeek() <= 7) {
    currentTime.w = RTC.getDayOfWeek();
  }
  currentTime.last = t;
}

float getTds() {
  // konstanta diperoleh dari percobaan. Hasil dari fungsi ini dibandingkan dengan tds meter standar. Hasilnya diregresikan
  // Baca README.md
  float C1 = 0, C2 = 45.9; // <- dari regresi linier, mungkin berbeda tergantung nilai R yg dipakai
  float x1, x2, ec;
  int i;

  // inisialisasi probe;
  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);

  /* dari pengalaman. kalau probe yang dicelupkan ke air diberi arus searah terus menerus akan terjadi elektrolisis
      sehinggah nilai pengukuran akan berubah.
      Karena itu sebelum pensamplingan nilai, probe terlebih dahulu diberi arus bolak-balik
  */
  for (i = 0; i < 5; i++) {
    digitalWrite(TDS_SOURCE_1, HIGH);
    digitalWrite(TDS_SOURCE_2, LOW);
    digitalWrite(TDS_SOURCE_1, LOW);
    digitalWrite(TDS_SOURCE_2, HIGH);
  }

  // sampling 1 -> PIN1 high, PIN2 low
  digitalWrite(TDS_SOURCE_1, HIGH);
  digitalWrite(TDS_SOURCE_2, LOW);
  x1 = analogRead(A0) / 1024.0;

  // sampling 2 -> PIN1 low, PIN2 high
  digitalWrite(TDS_SOURCE_2, HIGH);
  digitalWrite(TDS_SOURCE_1, LOW);
  x2 = analogRead(A0) / 1024.0;

  // konversi ke ec
  ec = x1 / (1 - x1) + (1 - x2) / x2;

  // kembalikan mode TWI
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  return C1 + C2 * ec;
}

void setPompa(boolean state, int duration) {
  digitalWrite(POMPA, state);
  if (state && duration > 0) {
    durations[T_POMPA] = duration;
  } else {
    durations[T_POMPA] = 0;
  }
}

void setValve(boolean state, int duration) {
  digitalWrite(VALVE, state);
  if (state && duration > 0) {
    durations[T_VALVE] = duration;
  } else {
    durations[T_VALVE] = 0;
  }
}

/*
   Jika kepekatan larutan kurang dari yang diharapkan.
   Selenoid valve akan dibuka selama waktu yang sebanding dengan kekurangannya.
*/
void nutrisiOtomatis() {
  int x = (int) getTds(), duration;
  int P = 4000; // kepekatan larutan tambahan
  float K = 100.0; // dari percobaan
  if (x < konfig.ppm) { // jika nutrisinya kurang
    duration = (int)(K * (konfig.ppm - x) / (P - konfig.ppm));
    lastAddedNutrision = duration;
    setValve(true, duration);
  }
}

void addLogText(String txt) {
  char ss[20];
  if (rtcValid) {
    snprintf(ss, 20, "/log/t%02d%02d%02dx.txt", currentTime.y, currentTime.m, currentTime.d);
  } else {
    snprintf(ss, 20, "/log/t%02d%02d%02dy.txt", currentTime.y, currentTime.m, currentTime.d);
  }
  File dataFile = SD.open(ss, FILE_WRITE);
  if (dataFile) {
    snprintf(ss, 20, "%02d:%02d:%02d: ", currentTime.h, currentTime.i, currentTime.s);
    dataFile.print(ss);
    dataFile.println(txt);
    dataFile.close();
  }
}

