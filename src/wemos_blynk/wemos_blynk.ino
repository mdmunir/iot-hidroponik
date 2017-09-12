/**
  Serial command adalah perintah untuk wemos yang dikirimkan dari serial port (serial monitor). Berikut beberapa perintah yang tersedia
  - ssid:<SSID>:<PASSWORD>  => Digunakan untuk mengganti koneksi wifi. Contoh ssid:cakmunir:rahasia
  - ip => Mendapatkan IP address dari wemos
  - tds => Membaca nilai kepekatan larutan
  - now => Membaca waktu sekarang dari RTC
  - set_time:<FORMAT> => Menset waktu dari RTC module. Format yang dipakai adalah "YY-MM-DD HH:ii:SS [W]". Contoh set_time:17-09-05 11:34:26 2
  - ppm:<VALUES> => Menset nilai dari beberapa pengaturan antara lain ppm, periode pompa, durasi nyala pompa. Contoh
                       ppm:1200 -> menset kepekatan larutan menadi 1200 ppm, periode nyala pompa 30 menit serta durasi pompa 5 menit.
  - timer:hh:ii-durasi => Menset skedul pada jam hh:ii apakah menyalah selama durasi menit ataukah mati.
  - timers => Menampilkan daftar skedule yang tersimpan.

  Fungsi-fungsi pin
  - RX dan TX Dicadangkan untuk komunikasi serial.
  - D3 dan D4 untuk source sensor tds (Baca readme)
  - A0 untuk input sensor tds.
  - D5, D6, D7, D8 terhubung dengan relay (D5 valve, D6 pompa, D7 dan D8 dicadangkan untuk nanti)
  - SDA dan SCL (D1 dan D2) terhubung dengan modul RTC


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

#include <RealTimeClockDS1307.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include <Time.h>

#define SCH_ADDRS 10
#define TDS_SOURCE_1 D3
#define TDS_SOURCE_2 D4

#define PPM_VALUE_PIN V0
#define BUTTON_PIN V1
#define PPM_KONFIG_PIN V2
#define ADDED_PPM_PIN V3
#define TIMER_PIN V4
#define TERMINAL_PIN V5


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
struct TState {
  byte pin;
  long after;
  long until;
};
enum TRelay {rValve, rPompa1, rPompa2, rPompa3};

struct TSchedule {
  byte menit, durasi;
};

TSerialProcess serialProcess;

TState relays[] = {
  {D5, 0, 0},
  {D6, 0, 0},
  {D7, 0, 0},
  {D8, 0, 0}
};



char formatted[32];
unsigned long prevMenit = 0, prevMillis = 0, prevTime = 0, nextReconnect = 0, nextNutrisi = 0, nextSchedule = 0, nextTds = 0;
boolean rtcValid = false;
int konfigPpm;
TSchedule schedules[24];

/* ******************************************************* */
//                     MAIN FUNCTION
/* ******************************************************* */
void setup(void) {
  byte x;
  int i, ppm;
  TSchedule sch;
  Serial.begin(115200);
  EEPROM.begin(256);

  // set mode output untuk tds sensor
  for (x = 0; x < 4; x++) {
    pinMode(relays[x].pin, OUTPUT);
  }
  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);

  serialProcess.addCommand("ssid", setSsidPass);
  serialProcess.addCommand("ip", getIp);
  serialProcess.addCommand("tds", samplingTds);
  serialProcess.addCommand("now", getTimeNow);
  serialProcess.addCommand("set_time", setTimeNow);
  serialProcess.addCommand("pins", getPinStates);
  serialProcess.addCommand("test", testTds);
  serialProcess.addCommand("ppm", setPpm);
  serialProcess.addCommand("analog", readAnalog);
  serialProcess.addCommand("timer", setTimer);
  serialProcess.addCommand("timers", getTimers);

  i = 0;
  i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 15) { // 15 detik
    serialProcess.processCommand();
    delay (1000);
    i++;
  }

  setSyncProvider(synchronCurrentTime);

  // read config
  // ppm
  EEPROM.get(0, ppm);
  if (ppm >= 500 && ppm <= 2500) {
    konfigPpm = ppm;
  } else {
    konfigPpm = 1000; // default
  }
  // scheduler
  for (x = 0; x < 24; x++) {
    EEPROM.get(SCH_ADDRS + x * sizeof(sch), sch);
    if (sch.menit < 60 && sch.durasi < 60) {
      schedules[x] = sch;
    } else {
      schedules[x].menit = 0;
      schedules[x].durasi = 0;
    }
  }
  initBlynk();

  Blynk.virtualWrite(PPM_KONFIG_PIN, konfigPpm);
}

/**
   Main process
*/
void loop ( void ) {
  byte x;
  TSchedule sch;
  int ppm;
  int jam, menit, m, MAX_MENIT = 24 * 60;
  long current = millis();

  serialProcess.processCommand();

  // schedule dari eeprom
  if (nextSchedule < current) {
    nextSchedule = current + 1000;
    jam = hour();
    menit = minute();

    if (prevMenit != menit) {
      prevMenit = menit;
      sch = schedules[jam];
      if (sch.durasi > 0 && sch.menit == menit) {
        pompaOn(sch.durasi * 60);
      }
    }
  }

  // nyalakan atau matikan relay sesuai delay dan durasi
  for (x = 0; x < 4; x++) {
    if (relays[x].after > 0 && relays[x].after < current) {
      digitalWrite(relays[x].pin, HIGH);
      relays[x].after = 0;
      if (x == rPompa1) {
        Blynk.virtualWrite(BUTTON_PIN, HIGH);
      }
    } else if (relays[x].until > 0 && relays[x].until < current) {
      digitalWrite(relays[x].pin, LOW);
      relays[x].until = 0;
      if (x == rPompa1) {
        Blynk.virtualWrite(BUTTON_PIN, LOW);
      }
    }
  }

  // nyalakan selenoid valve
  if (nextNutrisi > 0 && nextNutrisi <= current) {
    nutrisiOtomatis();
  }
  if (nextReconnect > 0 && nextReconnect < current) {
    if (WiFi.status() == WL_CONNECTED && !Blynk.connected()) {
      Blynk.connect();
    }

    if (!Blynk.connected()) {
      nextReconnect = current + 180 * 1000; // reconnect berikutnya 3 menit lagi
    } else {
      nextReconnect = 0;
    }
  }

  // kirim info tds tiap 10 menit
  if (nextTds < current) {
    nextTds = current + 600 * 1000;
    ppm = (int) getTds();
    Blynk.virtualWrite(PPM_VALUE_PIN, ppm);
  }

  if (Blynk.connected()) {
    Blynk.run();
  } else if (nextReconnect == 0) {
    nextReconnect = current + 60 * 1000; // jadwalkan reconect 1 menit lagi
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
    pompaOn(5 * 60); // nyala 5 menit
  } else {
    relayOff(rPompa1);
  }
}

BLYNK_WRITE(PPM_KONFIG_PIN)
{
  int inp = param.asInt();
  if (inp >= 500 && inp <= 2500) {
    konfigPpm = inp;
    EEPROM.put(0, konfigPpm);
    EEPROM.commit();
  }
}

BLYNK_WRITE(TIMER_PIN)
{
  TSchedule sch;
  long inp = param.asLong();
  int jam = inp / 10000, durasi;
  inp = inp % 10000;

  sch.menit = inp / 100;
  durasi = inp % 100;
  if (jam >= 0 && jam < 24 && sch.menit >= 0 && sch.menit < 60) {
    sch.durasi = (durasi > 0 && durasi < 60) ? durasi : 0;
    schedules[jam] = sch;
    EEPROM.put(SCH_ADDRS + jam * sizeof(sch), sch);
    EEPROM.commit();
  }
}

BLYNK_WRITE(InternalPinRTC) {
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013
  unsigned long blynkTime = param.asLong();

  if (blynkTime >= DEFAULT_TIME) {
    setTime(blynkTime);
    rtcValid = true;
    Serial.print("Server time: ");
    Serial.println(blynkTime);
  }
}

/* ******************************************************* */
//                     SERIAL FUNCTION
/* ******************************************************* */
void getTimeNow(String s) {
  snprintf(formatted, 32, "%d-%02d-%02d %02d:%02d:%02d %d", year(), month(), day(), hour(), minute(), second(), weekday());
  Serial.println(formatted);
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
    Serial.print(WiFi.localIP());
    Serial.print(" | ");
    Serial.println(Blynk.connected() || Blynk.connect() ? "online" : "offline");
  } else {
    Serial.println("Not connected");
  }
}

void samplingTds(String s) {
  float x = getTds();
  Serial.println(x);
}

void setPpm(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= 500 && inp <= 2500) {
    konfigPpm = inp;
    EEPROM.put(0, konfigPpm);
    EEPROM.commit();
    Blynk.virtualWrite(PPM_KONFIG_PIN, konfigPpm);
  }
  Serial.println(konfigPpm);
}

void setTimer(String s) {
  TSchedule sch;
  int i = 0, inp;
  byte x;
  // jam
  inp = getIntFromStr(s, i);
  if (inp >= 24) {
    return;
  }

  x = inp;
  i++;
  // menit
  inp = getIntFromStr(s, i);
  if (inp >= 60) {
    return;
  }
  sch.menit = inp;
  i++;
  inp = getIntFromStr(s, i);
  sch.durasi = inp < 60 ? inp : 0;
  schedules[x] = sch;
  EEPROM.put(SCH_ADDRS + x * sizeof(sch), sch);
  EEPROM.commit();
  Serial.print(x);
  Serial.print(':');
  Serial.print(sch.menit);
  Serial.print(" -> ");
  Serial.println((sch.menit < 60 && sch.durasi > 0 && sch.durasi < 60) ? String(sch.durasi) : " OFF");
}

String _getTimers() {
  String s = "";
  byte x;
  TSchedule sch;
  for (x = 0; x < 24; x++) {
    sch = schedules[x];
    s += String(x) + ":";
    s += (sch.menit < 60) ? String(sch.menit) : "0";
    s += " -> ";
    s += (sch.menit < 60 && sch.durasi > 0 && sch.durasi < 60) ? String(sch.durasi) : " OFF";
    s += "\n";
  }
  return s;
}

void getTimers(String s) {
  Serial.print(_getTimers());
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
  float f;

  for (i = 0; i < 4; i++) {
    digitalWrite(TDS_SOURCE_1, i % 2 == 0);
    digitalWrite(TDS_SOURCE_2, i % 2 == 1);
  }
  // sampling 1 -> PIN1 high, PIN2 low
  digitalWrite(TDS_SOURCE_1, HIGH);
  digitalWrite(TDS_SOURCE_2, LOW);
  a1 = analogRead(A0);

  for (i = 0; i < 4; i++) {
    digitalWrite(TDS_SOURCE_1, i % 2 == 1);
    digitalWrite(TDS_SOURCE_2, i % 2 == 0);
  }
  // sampling 2 -> PIN1 low, PIN2 high
  digitalWrite(TDS_SOURCE_2, HIGH);
  digitalWrite(TDS_SOURCE_1, LOW);
  a2 = analogRead(A0);

  // idle
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  f = 1.0 * a1 / a2;

  Serial.print("A1 = ");
  Serial.print(a1);
  Serial.print(" | A2 = ");
  Serial.print(a2);
  Serial.print(" | tds = ");
  Serial.print(f);
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

time_t synchronCurrentTime() {
  tmElements_t tm;
  time_t t;
  RTC.readClock();
  if (RTC.getHours() < 24 && RTC.getMinutes() < 60 && RTC.getSeconds() < 60 && RTC.getYear() < 99 && RTC.getMonth() <= 12 && RTC.getDate() <= 31) {
    // rtcValid
    tm.Year = RTC.getYear() + 30;
    tm.Month = RTC.getMonth();
    tm.Day = RTC.getDate();
    tm.Hour = RTC.getHours();
    tm.Minute = RTC.getMinutes();
    tm.Second = RTC.getSeconds();
    t = makeTime(tm);
    rtcValid = true;
    return t;
  } else if (!rtcValid) {
    Blynk.sendInternal("rtc", "sync");
  }
  return 0;
}

float getTds() {
  // konstanta diperoleh dari percobaan. Hasil dari fungsi ini dibandingkan dengan tds meter standar. Hasilnya diregresikan
  // Baca README.md
  float C1 = -15.0, C2 = 620.0; // <- dari regresi linier, mungkin berbeda tergantung nilai R yg dipakai
  float x1, x2, ec;
  int i;

  // inisialisasi probe;

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

void relayOn(byte relay, unsigned int duration, unsigned int after) {
  if (after == 0) {
    relays[relay].after = 0;
    digitalWrite(relays[relay].pin, HIGH);
    if (relay == rPompa1) {
      Blynk.virtualWrite(V1, HIGH);
    }
  } else {
    relays[relay].after = millis() + after * 1000;
  }
  relays[relay].until = (duration == 0) ? 0 : millis() + (after + duration) * 1000;
}

void relayOff(byte relay) {
  digitalWrite(relays[relay].pin, LOW);
  relays[relay].until = 0;
  relays[relay].after = 0;
  if (relay == rPompa1) {
    Blynk.virtualWrite(V1, LOW);
  }
}

/*
   Jika kepekatan larutan kurang dari yang diharapkan.
   Selenoid valve akan dibuka selama waktu yang sebanding dengan kekurangannya.
*/
void nutrisiOtomatis() {
  long t = now(), selisih = t - prevTime;
  prevTime = t;
  float x = getTds();
  int P = 4000; // kepekatan larutan tambahan
  float K = 100.0, duration = 0; // dari percobaan

  nextNutrisi = 0; // tunggu schedule berikutnya
  if (x < konfigPpm) { // jika nutrisinya kurang
    duration = K * (konfigPpm - x) / (P - konfigPpm);
    if (duration > 60) {
      duration = 60;
    }
    relayOn(rValve, (int)duration, 0);
  }
  Blynk.virtualWrite(V0, x); // nilai ppm
  Blynk.virtualWrite(V3, 1000 * duration / selisih);
}

void pompaOn(int duration) {
  nextNutrisi = millis() + (duration < 180 ? duration : 180) * 1000; // tambah nutrisi setelah pompa mati
  relayOn(rPompa1, duration, 0);
}

