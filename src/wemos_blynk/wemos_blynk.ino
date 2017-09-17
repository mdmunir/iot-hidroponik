/**
  Serial command adalah perintah untuk wemos yang dikirimkan dari serial port (serial monitor). Berikut beberapa perintah yang tersedia
  - ssid:<SSID>:<PASSWORD>  => Digunakan untuk mengganti koneksi wifi. Contoh ssid:cakmunir:rahasia
  - ip => Mendapatkan IP address dari wemos
  - tds => Membaca nilai kepekatan larutan
  - now => Membaca waktu sekarang dari RTC
  - ppm:<VALUES> => Menset nilai dari beberapa pengaturan antara lain ppm, periode pompa, durasi nyala pompa. Contoh
                       ppm:1200 -> menset kepekatan larutan menadi 1200 ppm, periode nyala pompa 30 menit serta durasi pompa 5 menit.

  Fungsi-fungsi pin
  - RX dan TX Dicadangkan untuk komunikasi serial.
  - D3 dan D4 untuk source sensor tds (Baca readme)
  - A0 untuk input sensor tds.
  - D5, D6, D7, D8 terhubung dengan relay (D5 valve, D6 pompa, D7 dan D8 dicadangkan untuk nanti)


  >>Tambahan: Fungsi `initBlynk()` di definisikan dalam file lain. Tambah new tab, isi nama file denga "token". Lalu isi file tersebut dengan code berikut

  #include <ESP8266WiFi.h>
  #include <BlynkSimpleEsp8266.h>

  void initBlynk() {
    char auth[] = "YourToken";
    char ssid[] = "ssid-name";
    char pass[] = "password";
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      delay(500);
    }
    Blynk.config(auth);
    Blynk.connect();
  }

  -----------------
  Program ini memnggunakan library Blynk. Baca cara instalnya di http://www.blynk.cc/
*/

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include <Time.h>
#include <WidgetRTC.h>


//#define BLYNK_DEBUG // Optional, this enables lots of prints
#define BLYNK_PRINT Serial

#define TDS_SOURCE_1 D0
#define TDS_SOURCE_2 D3
#define PUSH_BUTTON D4

#define PPM_VALUE_PIN V0
#define BUTTON_PIN V1
#define PPM_KONFIG_PIN V2
#define PERIODE_KONFIG_PIN V3
#define DURASI_KONFIG_PIN V4
#define TERMINAL_PIN V5
#define HISTORY_PIN V6
#define DISPLAY_TIME_PIN V7;

/* *************************************************** */
//              CLASS PROCESS SERIAL
/* *************************************************** */

WidgetTerminal terminal(TERMINAL_PIN);
WidgetRTC rtc;

typedef String (*TStringCallback)(String);
enum TCmdType {serialType, terminalType};

class TCommand {
  private:
    String _cmd;
    TStringCallback _callback;
    TCommand* _next;

  public:
    TCommand(String cmd, TStringCallback callback) {
      _cmd = cmd;
      _callback = callback;
    }
    TCommand* next() {
      return _next;
    }
    void next(TCommand* next) {
      _next = next;
    }
    boolean process(String cmd, String content, TCmdType t) {
      String s;
      if (_cmd == cmd) {
        s = _callback(content);
        switch (t) {
          case serialType:
            Serial.println(s);
            break;
          case terminalType:
            terminal.println(s);
            terminal.flush();
            break;
        }
        return true;
      }
      return false;
    }
};

class TProcess {
  private:
    TCommand* _first;
    TCommand* _last;
    String _cmd;
    String _param;
    TStringCallback _customProcess;

  public:
    String cmd() {
      return _cmd;
    }
    String param() {
      return _param;
    }
    void setCustomProcess(TStringCallback callback) {
      _customProcess = callback;
    }

    void addCommand(String cmd, TStringCallback callback) {
      TCommand *sc = new TCommand(cmd, callback);
      if (!_last) {
        _first = sc;
        _last = sc;
      } else {
        _last->next(sc);
        _last = sc;
      }
    }
    void processSerial() {
      if (Serial.available()) {
        processCommand(Serial.readString(), serialType);
      }
    }
    void processCommand(String s, TCmdType t) {
      int i = s.indexOf(':');
      TCommand *sc = _first;
      if (i > 0) {
        _cmd = s.substring(0, i);
        _param = s.substring(i + 1);
      } else {
        _cmd = s;
        _param = "";
      }
      while (sc) {
        if (sc->process(_cmd, _param, t)) {
          return;
        } else {
          sc = sc->next();
        }
      }

      if (_customProcess) {
        _customProcess(s);
      } else {
        switch (t) {
          case serialType:
            Serial.print("Unknown command: ");
            Serial.println(s);
            break;
          case terminalType:
            terminal.println( "Unknown command: " + s);
            terminal.flush();
            break;
        }
      }
    }
};

class TRelay {
  private:
    byte _pin;
    long _after;
    long _until;
    int _vpin;
  public:
    TRelay(byte pin) {
      _pin = pin;
      _after = 0;
      _until = 0;
      pinMode(_pin, OUTPUT);
    }
    TRelay(byte pin, int vpin) {
      _pin = pin;
      _after = 0;
      _until = 0;
      pinMode(_pin, OUTPUT);
      _vpin = vpin;
    }
    void run() {
      if (_after > 0 && _after < millis()) {
        _after = 0;
        digitalWrite(_pin, HIGH);
        if (_vpin) {
          Blynk.virtualWrite(_vpin, HIGH);
        }
      }
      if (_until > 0 && _until < millis()) {
        _until = 0;
        digitalWrite(_pin, LOW);
        if (_vpin) {
          Blynk.virtualWrite(_vpin, LOW);
        }
      }
    }

    void on(unsigned int durasi, unsigned int after) {
      _after = millis() + after * 1000;
      _until = (durasi > 0) ? millis() + (after + durasi) * 1000 : 0;
    }

    void on(unsigned int durasi) {
      _after = 0;
      _until = (durasi > 0) ? millis() + durasi * 1000 : 0;

      digitalWrite(_pin, HIGH);
      if (_vpin) {
        Blynk.virtualWrite(_vpin, HIGH);
      }
    }

    void off() {
      _until = 0;
      _after = 0;
      digitalWrite(_pin, LOW);
      if (_vpin) {
        Blynk.virtualWrite(_vpin, LOW);
      }
    }

    boolean state() {
      return digitalRead(_pin);
    }
};

/* *************************************************** */
//              GLOBAL VAR
/* *************************************************** */
struct TConfig {
  int ppm;
  int periode; // dalam menit
  int durasi; // dalam detik
};

TProcess process;
TConfig konfig;
TRelay valve(D5), pompa1(D6, BUTTON_PIN);

/* ******************************************************* */
//                     MAIN FUNCTION
/* ******************************************************* */
void setup(void) {
  int i;
  TConfig k;
  Serial.begin(115200);
  EEPROM.begin(64);

  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);
  pinMode(PUSH_BUTTON, INPUT_PULLUP);

  process.addCommand("ssid", setSsidPass);
  process.addCommand("ip", getIp);
  process.addCommand("mac", getMac);
  process.addCommand("tds", samplingTds);
  process.addCommand("now", getTimeNow);
  process.addCommand("ppm", setPpm);
  process.addCommand("periode", setPeriode);
  process.addCommand("durasi", setDurasi);
  process.addCommand("analog", readAnalog);
  process.addCommand("on", setPompaOn);
  process.addCommand("sync", synchronData);

  i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 15) { // 15 detik
    process.processSerial();
    delay (1000);
    i++;
  }

  // read config
  // ppm
  EEPROM.get(0, k);
  konfig.ppm = (k.ppm >= 500 && k.ppm <= 2500) ? k.ppm : 1000; // default 1000
  konfig.periode = (k.periode >= 10 && k.periode <= 360) ? k.periode : 30; // default 30 menit
  konfig.durasi = (k.durasi >= 15 && k.durasi <= 600) ? k.durasi : 60; // default 60 detik

  initBlynk();
  // Begin synchronizing time
  rtc.begin();
}

/**
   Main process
*/
void loop(void) {
  process.processSerial();
  handlePushButton();
  handleSchedule();
  handleRelays();
  handleSynchron();
  handleNutrisi();

  if (Blynk.connected()) {
    Blynk.run();
  } else {
    handleReconnect();
  }
}


/* ******************************************************* */
//                     BLYNK FUNCTION
/* ******************************************************* */
BLYNK_CONNECTED() {
  Blynk.syncAll();
}

BLYNK_WRITE(BUTTON_PIN)
{
  int val = param.asInt();
  if (val) { // on
    pompaOn(konfig.durasi < 120 ? konfig.durasi : 120); // nyala 1 menit
  } else {
    pompa1.off();
  }
}

BLYNK_WRITE(PPM_KONFIG_PIN)
{
  int inp = param.asInt();
  if (inp >= 500 && inp <= 2500) {
    konfig.ppm = inp;
    EEPROM.put(0, konfig);
    EEPROM.commit();
  }
}

BLYNK_WRITE(PERIODE_KONFIG_PIN)
{
  int inp = param.asInt();
  if (inp >= 10 && inp <= 360) {
    konfig.periode = inp;
    EEPROM.put(0, konfig);
    EEPROM.commit();
  }
}

BLYNK_WRITE(DURASI_KONFIG_PIN)
{
  int inp = param.asInt();
  if (inp >= 15 && inp <= 600) {
    konfig.durasi = inp;
    EEPROM.put(0, konfig);
    EEPROM.commit();
  }
}

BLYNK_WRITE(TERMINAL_PIN)
{
  String s = param.asString();
  process.processCommand(s, terminalType);
}
//* ******************************************************* */
//                     LOOP FUNCTION
//* ******************************************************* */

boolean prevPushState = false;
long prevPushTime = 0;
void handlePushButton() {
  boolean pushed = !digitalRead(PUSH_BUTTON);
  if (pushed) {
    if (prevPushState) { // pushed
      if (millis() - prevPushTime > 50) {
        prevPushTime = millis() + 10000; // tunggu 10 detik
        if (pompa1.state()) {
          pompa1.off();
        } else {
          pompaOn(konfig.durasi < 120 ? konfig.durasi : 120);
        }
      }
    } else if (prevPushTime < millis()) {
      prevPushTime = millis();
    }
    prevPushState = true;
  } else {
    prevPushState = false;
  }
}

void handleRelays() {
  valve.run();
  pompa1.run();
}

/*
   Jika kepekatan larutan kurang dari yang diharapkan.
   Selenoid valve akan dibuka selama waktu yang sebanding dengan kekurangannya.
*/
long prevTimeNutrisi = 0, nextNutrisi = 0;
void handleNutrisi() {
  long t = millis() / 1000, selisih;
  float x;
  int P = 4000; // kepekatan larutan tambahan
  float K = 100.0, duration = 0; // dari percobaan
  if (nextNutrisi > 0 && nextNutrisi < millis()) {
    nextNutrisi = 0; // tunggu schedule berikutnya
    selisih = t - prevTimeNutrisi;
    prevTimeNutrisi = t;

    x = getTds();
    if (x < konfig.ppm) { // jika nutrisinya kurang
      duration = K * (konfig.ppm - x) / (P - konfig.ppm);
      if (duration > 60) {
        duration = 60;
      }
      valve.on((int)duration);
    }
    Blynk.virtualWrite(PPM_VALUE_PIN, x); // nilai ppm
    Blynk.virtualWrite(HISTORY_PIN, 1000 * duration / selisih);
  }
}

long nextSchedule = 600 * 1000; // sekedul pertama 5 menit setelah nyalah
void handleSchedule() {
  // scheduler
  if (nextSchedule < millis()) {
    if (!pompa1.state()) {
      pompaOn(konfig.durasi);
    }
  }
}

long nextReconnect = 0;
void handleReconnect() {
  if (nextReconnect < millis()) {
    if (WiFi.status() != WL_CONNECTED) {
      initBlynk();
    } else if (!Blynk.connected()) {
      Blynk.connect();
    }
    nextReconnect = millis() + 300 * 1000; // 5 menit lagi
  }
}

long nextSync = 0;
void handleSynchron() {
  if (nextSync < millis()) {
    nextSync = millis() + 600 * 1000; // 10 menit lagi
    synchronData("");
  }
}

//* ******************************************************* */
//                     SERIAL FUNCTION
//* ******************************************************* */
String getTimeNow(String s) {
  char formatted[32];
  snprintf(formatted, 32, "%d-%02d-%02d %02d:%02d:%02d %d", year(), month(), day(), hour(), minute(), second(), weekday());
  return String(formatted);
}

String setSsidPass(String s) {
  String result = "";
  char ssid[40], pass[40];
  int i = s.indexOf(':');
  IPAddress ip;
  if (i >= 0) {
    s.substring(0, i).toCharArray(ssid, 40);
    s.substring(i + 1).toCharArray(pass, 40);
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect();
    }
    WiFi.begin(ssid, pass);
    for (i = 0; i < 10; i++) { // tunggu 10 detik
      if (WiFi.status() == WL_CONNECTED) {
        ip = WiFi.localIP();
        result += "Connect to [";
        result += ssid;
        result += "] Ip address: ";
        result += String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        return result;
      }
      delay(1000);
    }
  }
}

String getIp(String s) {
  String result = "";
  IPAddress ip;
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    result += "Connect to [";
    result += WiFi.SSID();
    result += "] Ip address: ";
    result += String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
    result += " | ";
    result += Blynk.connected() || Blynk.connect() ? "online" : "offline";
    return result;
  } else {
    return "offline";
  }
}

String getMac(String s) {
  char r[20];
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(r, 20, "%02x:%02x:%02x:%02x:%02x:%02x", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  return String(r);
}

String samplingTds(String s) {
  float x = getTds();
  return String(x);
}

String setPpm(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= 500 && inp <= 2500) {
    konfig.ppm = inp;
    EEPROM.put(0, konfig);
    EEPROM.commit();
    Blynk.virtualWrite(PPM_KONFIG_PIN, konfig.ppm);
  }
  return String(konfig.ppm);
}

String setPeriode(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= 10 && inp <= 360) {
    konfig.periode = inp;
    EEPROM.put(0, konfig);
    EEPROM.commit();
    Blynk.virtualWrite(PERIODE_KONFIG_PIN, konfig.periode);
  }
  return String(konfig.periode);
}

String setDurasi(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= 15 && inp <= 600) {
    konfig.durasi = inp;
    EEPROM.put(0, konfig);
    EEPROM.commit();
    Blynk.virtualWrite(DURASI_KONFIG_PIN, konfig.durasi);
  }
  return String(konfig.durasi);
}


String readAnalog(String s) {
  return String(analogRead(A0));
}

String setPompaOn(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp < 1) {
    inp = 1;
  } else if (inp > 5) {
    inp = 5;
  }
  pompaOn(inp * 60);
  return "ON " + String(inp) + " menit";
}

String synchronData(String s) {
  Blynk.virtualWrite(PPM_VALUE_PIN, getTds());
  Blynk.virtualWrite(BUTTON_PIN, pompa1.state());
  Blynk.virtualWrite(PPM_KONFIG_PIN, konfig.ppm);
  Blynk.virtualWrite(PERIODE_KONFIG_PIN, konfig.periode);
  Blynk.virtualWrite(DURASI_KONFIG_PIN, konfig.durasi);
  return "Done";
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

float getTds() {
  // konstanta diperoleh dari percobaan. Hasil dari fungsi ini dibandingkan dengan tds meter standar. Hasilnya diregresikan
  // Baca README.md
  float C1 = -15.0, C2 = 620.0; // <- dari regresi linier, mungkin berbeda tergantung nilai R yg dipakai
  float x1, x2, ec;
  int i;

  /* dari pengalaman. kalau probe yang dicelupkan ke air diberi arus searah terus menerus akan terjadi elektrolisis
      sehinggah nilai pengukuran akan berubah.
      Karena itu sebelum pensamplingan nilai, probe terlebih dahulu diberi arus bolak-balik
  */
  for (i = 0; i < 4; i++) {
    digitalWrite(TDS_SOURCE_1, i % 2 == 0);
    digitalWrite(TDS_SOURCE_2, i % 2 == 1);
  }
  // sampling 1 -> PIN1 high, PIN2 low
  digitalWrite(TDS_SOURCE_1, HIGH);
  digitalWrite(TDS_SOURCE_2, LOW);
  x1 = analogRead(A0) / 1023.0;

  for (i = 0; i < 4; i++) {
    digitalWrite(TDS_SOURCE_1, i % 2 == 1);
    digitalWrite(TDS_SOURCE_2, i % 2 == 0);
  }
  // sampling 2 -> PIN1 low, PIN2 high
  digitalWrite(TDS_SOURCE_2, HIGH);
  digitalWrite(TDS_SOURCE_1, LOW);
  x2 = analogRead(A0) / 1023.0;
  // idle
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  // konversi ke ec
  ec = 0.5 * (x1 / (1 - x1) + (1 - x2) / x2);
  return C1 + C2 * ec;
}

void pompaOn(int duration) {
  nextNutrisi = millis() + (duration < 180 ? duration : 180) * 1000; // tambah nutrisi setelah pompa mati
  nextSchedule = millis() + konfig.periode * 60 * 1000;
  pompa1.on(duration);
}

