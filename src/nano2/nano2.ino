/**
  Serial command adalah perintah untuk wemos yang dikirimkan dari serial port (serial monitor). Berikut beberapa perintah yang tersedia
  - tds => Membaca nilai kepekatan larutan
  - now => Membaca waktu sekarang dari RTC
  - set_time:<FORMAT> => Menset waktu dari RTC module. Format yang dipakai adalah "YY-MM-DD HH:ii:SS [W]". Contoh set_time:17-09-05 11:34:26 2
  - ppm:<VALUES> => Menset nilai dari beberapa pengaturan antara lain ppm, periode pompa, durasi nyala pompa. Contoh
                       ppm:1200 -> menset kepekatan larutan menadi 1200 ppm, periode nyala pompa 30 menit serta durasi pompa 5 menit.
  - timer:hh:ii-durasi => Menset skedul pada jam hh:ii apakah menyalah selama durasi menit ataukah mati.
  - timers => Menampilkan daftar skedule yang tersimpan.

  Fungsi-fungsi pin
  - 0 dan 1 (RX dan TX) Dicadangkan untuk komunikasi serial.
  - 2 dan 3 untuk source sensor tds
  - A0 untuk input sensor tds
  - 4, 5, 6, 7 terhubung dengan relay 
  - A4 dan A5 (SDA dan SCL) terhubung dengan modul RTC
*/

#include <RealTimeClockDS1307.h>
#include <EEPROM.h>

#define SCH_ADDRS 10
#define TDS_SOURCE_1 2
#define TDS_SOURCE_2 3


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
  int delay;
  long duration;
};
enum TRelay {rValve, rPompa1, rPompa2, rPompa3};

struct TSchedule {
  byte menit, durasi;
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


TSerialProcess serialProcess;

TState relays[] = {
  {4, 0, 0},
  {5, 0, 0},
  {6, 0, 0},
  {7, 0, 0}
};
char formatted[] = "00-00-00 00:00:00x";
long seconds, lastSeconds = 0;
int lastAddedNutrision;
boolean rtcValid = false;
int konfigPpm;
TSchedule schedules[24];

/* ******************************************************* */
//                     MAIN FUNCTION
/* ******************************************************* */
void setup(void) {
  byte x;
  int ppm;
  TSchedule sch;
  Serial.begin(115200);
  EEPROM.begin();

  // set mode output untuk tds sensor
  for (x = 0; x < 4; x++) {
    pinMode(relays[x].pin, OUTPUT);
  }
  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);

  serialProcess.addCommand("tds", samplingTds);
  serialProcess.addCommand("now", getTimeNow);
  serialProcess.addCommand("set_time", setTimeNow);
  serialProcess.addCommand("ppm", setPpm);
  serialProcess.addCommand("timer", setTimer);
  serialProcess.addCommand("timers", getTimers);

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
}

/**
   Main process
*/
void loop ( void ) {
  byte x;
  TSchedule sch;
  serialProcess.processCommand();

  seconds = millis() / 1000;
  if (seconds != lastSeconds) { // ganti detik
    lastSeconds = seconds;
    getCurrentTime();

    // nyalakan atau matikan relay
    for (x = 0; x < 4; x++) {
      if (relays[x].delay > 0) {
        relays[x].delay--;
        if (relays[x].delay == 0) {
          digitalWrite(relays[x].pin, HIGH);
        }
      } else if (relays[x].duration > 0) {
        relays[x].duration--;
        if (relays[x].duration == 0) {
          digitalWrite(relays[x].pin, LOW);
        }
      }
    }

    if (seconds % 60 == 0) { // ganti menit
      sch = schedules[currentTime.h];
      if (sch.durasi > 0 && sch.menit == currentTime.i) {
        nutrisiOtomatis(sch.durasi * 60);
      }
    }
  }
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

void samplingTds(String s) {
  float x = getTds();
  Serial.println(x);
}

void setPpm(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= 500 && inp <= 2500) {
    konfigPpm = inp;
    EEPROM.put(0, konfigPpm);
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
}

void getTimers(String s) {
  byte x;
  TSchedule sch;
  for (x = 0; x < 24; x++) {
    sch = schedules[x];
    Serial.print(x);
    Serial.print(':');
    if (sch.menit < 60) {
      Serial.print(sch.menit);
    } else {
      Serial.print('0');
    }
    Serial.print(" -> ");
    if (sch.menit < 60 && sch.durasi > 0 && sch.durasi < 60) {
      Serial.print(sch.durasi);
    } else {
      Serial.print(" OFF");
    }
    Serial.println();
  }
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

  if (RTC.getYear() < 99 && RTC.getMonth() <= 12 && RTC.getDate() <= 31) { // RTC valid
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
  float C1 = 27.0, C2 = 61.4; // <- dari regresi linier, mungkin berbeda tergantung nilai R yg dipakai
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

void relayOn(byte relay, long duration, int after) {
  relays[relay].delay = after;
  if (after == 0) {
    digitalWrite(relays[relay].pin, HIGH);
  }
  relays[relay].duration = duration;
}

void relayOff(byte relay) {
  digitalWrite(relays[relay].pin, LOW);
  relays[relay].duration = 0;
  relays[relay].delay = 0;
}

/*
   Jika kepekatan larutan kurang dari yang diharapkan.
   Selenoid valve akan dibuka selama waktu yang sebanding dengan kekurangannya.
*/
void nutrisiOtomatis(int durasiPompa) {
  int x = (int) getTds(), duration;
  int P = 4000; // kepekatan larutan tambahan
  float K = 100.0; // dari percobaan
  if (x < konfigPpm) { // jika nutrisinya kurang
    duration = (int)(K * (konfigPpm - x) / (P - konfigPpm));
    lastAddedNutrision = duration;
    relayOn(rValve, duration < 60 ? duration : 60, 0);
  } else {
    lastAddedNutrision = 0;
  }

  if (durasiPompa > 0) {
    relayOn(rPompa1, durasiPompa, 0);
    // pompa2
    // relayOn(rPompa2, durasiPompa, 1 * (durasiPompa + 1));
  }
}

