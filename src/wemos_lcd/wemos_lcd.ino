/**
  Serial command adalah perintah untuk wemos yang dikirimkan dari serial port (serial monitor). Berikut beberapa perintah yang tersedia
  - tds => Membaca nilai kepekatan larutan
  - now => Membaca waktu sekarang dari RTC
  - ppm:<VALUES> => Menset nilai dari beberapa pengaturan antara lain ppm, periode pompa, durasi nyala pompa. Contoh
                       ppm:1200 -> menset kepekatan larutan menadi 1200 ppm, periode nyala pompa 30 menit serta durasi pompa 5 menit.

  Fungsi-fungsi pin
  - RX dan TX Dicadangkan untuk komunikasi serial.
  - D0 dan D3 untuk source sensor tds (Baca readme)
  - D4 untuk push button
  - A0 untuk input sensor tds.
  - D5, D6 terhubung dengan relay (D5 valve, D6 pompa)
  - D7 untuk IR remote
*/

#include <EEPROM.h>
#include <TimeLib.h>
#include <LiquidCrystal_I2C.h>
#include <Button.h>
#include <Wire.h>

#if defined(ARDUINO_ESP8266_WEMOS_D1MINI)    // wemos

#define TDS_SOURCE_1 D0
#define TDS_SOURCE_2 D3
#define RELAY_VALVE D4
#define RELAY_POMPA1 D5
#define RECV_PIN D7

#define B_DOWN D6
#define B_OK D7
#define B_UP D8

#define IS_WEMOS true

#else // arduino

#define TDS_SOURCE_1 2
#define TDS_SOURCE_2 5
#define RELAY_VALVE 7
#define RELAY_POMPA1 8
#define RECV_PIN 4

#define B_DOWN 10
#define B_UP 11
#define B_OK 12
#endif

// kode IRremote
#define IR_UP 0xFFFF01
#define IR_DOWN 0xFFFF02
#define IR_LEFT 0xFFFF03
#define IR_RIGHT 0xFFFF04
#define IR_OK 0xFFFF05
#define IR_MENU 0xFFFF06

// batas konfig
#define PPM_MIN 50 // dikali 10
#define PPM_MAX 300
#define DURATION_MIN 15 // detik
#define DURATION_MAX 300
#define PERIODE_MIN 15 // menit
#define PERIODE_MAX 360 // 6 jam

#define NEXT_READ_TDS 600 // 10 menit

/* *************************************************** */
//              CLASS PROCESS SERIAL
/* *************************************************** */

unsigned long detik() {
  return millis() / 1000;
}

typedef String (*TStringCallback)(String);
enum TEnumAction {acUp, acDown, acLeft, acRight, acOk, acMenu} ;

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
    boolean process(String cmd, String content) {
      String s;
      if (_cmd == cmd) {
        s = _callback(content);
        Serial.println(s);
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
        processCommand(Serial.readString());
      }
    }
    void processCommand(String s) {
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

/* Active LOW */
class TRelay {
  private:
    byte _pin;
    long _after;
    long _until;
    void _on() {
      digitalWrite(_pin, LOW);
    }
    void _off() {
      digitalWrite(_pin, HIGH);
    }
  public:
    TRelay(byte pin) {
      _pin = pin;
      _after = 0;
      _until = 0;
      pinMode(_pin, OUTPUT);
      _off();
    }
    void run() {
      if (_after > 0 && _after < detik()) {
        _after = 0;
        _on();
      }
      if (_until > 0 && _until < detik()) {
        _until = 0;
        _off();
      }
    }

    void on(unsigned int durasi, unsigned int after = 0) {
      if (after == 0) {
        _after = 0;
        _on();
      } else {
        _after = detik() + after;
      }
      _until = (durasi > 0) ? detik() + (after + durasi) : 0;
    }

    void off() {
      _until = 0;
      _after = 0;
      _off();
    }

    boolean state() {
      return !digitalRead(_pin);
    }
};

/* *************************************************** */
//              GLOBAL VAR
/* *************************************************** */

struct TConfig {
  int ppm;
  int periode; // dalam menit
  int duration; // dalam detik
  float tdsFactor;
};
struct TAppState {
  byte current, last;
  float rawTds;
  long nextSave, lightOff;
  bool changed;
};

TAppState appState;
Button bUp(B_UP, BUTTON_PULLDOWN), bDown(B_DOWN, BUTTON_PULLDOWN), bOk(B_OK, BUTTON_PULLDOWN);

TProcess process;
TConfig konfig;
TRelay valve(RELAY_VALVE), pompa1(RELAY_POMPA1);

LiquidCrystal_I2C lcd(0x27, 16, 2);

/* ******************************************************* */
//                     MAIN FUNCTION
/* ******************************************************* */
void setup(void) {
  Wire.begin();
  TConfig k;
  Serial.begin(115200);
  eepromBegin();

  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);

  process.addCommand("tds", samplingTds);
  process.addCommand("now", getTimeNow);
  process.addCommand("ppm", setPpm);
  process.addCommand("periode", setPeriode);
  process.addCommand("durasi", setDurasi);
  process.addCommand("on", setPompaOn);
  process.addCommand("scan", scanI2C);
  process.addCommand("print", printLcd);

  // read config
  // ppm
  EEPROM.get(0, k);
  konfig.ppm = (k.ppm >= PPM_MIN && k.ppm <= PPM_MAX) ? k.ppm : 100; // dikali 10. default 100 (1000 ppm).
  konfig.periode = (k.periode >= PERIODE_MIN && k.periode <= PERIODE_MAX) ? k.periode : 60; // default 60 menit
  konfig.duration = (k.duration >= DURATION_MIN && k.duration <= DURATION_MAX) ? k.duration : 20; // default 20 detik
  konfig.tdsFactor = (k.tdsFactor > 0.0 && k.tdsFactor <= 5000.0) ? k.tdsFactor : 250.0; // default 250

  lcd.begin();
  lcd.clear();

  appState.current = 0;
  appState.last = 99;
  appState.lightOff = 60;

  bOk.releaseHandler(onOkRelease);
  bUp.holdHandler(onUpHold, 50);
  bDown.holdHandler(onDownHold, 50);
  bOk.holdHandler(onOkHold, 2000);
}

/**
   Main process
*/
void loop(void) {
  process.processSerial();
  handleDisplay();
  handleSave();
  handleSchedule();
  handleRelays();
  handleNutrisi();
  handleButton();
}


//* ******************************************************* */
//                     LOOP FUNCTION
//* ******************************************************* */
void onUpHold(Button &btn) {
  handleAction(acUp);
}

void onDownHold(Button &btn) {
  handleAction(acDown);
}

void onOkHold(Button &btn) {
  handleAction(acOk);
}

void onOkRelease(Button &btn) {
  if (btn.holdTime() > 50 && btn.holdTime() < 2000) {
    handleAction(acMenu);
  }
}

void handleButton() {
  bUp.isPressed();
  bDown.isPressed();
  bOk.isPressed();
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
  long t = detik(), selisih;
  int x, P = 4000; // kepekatan larutan tambahan
  int konfigPpm = konfig.ppm * 10;
  float K = 100.0, duration = 0; // dari percobaan
  if (nextNutrisi > 0 && nextNutrisi < detik()) {
    nextNutrisi = 0; // tunggu schedule berikutnya
    selisih = t - prevTimeNutrisi;
    prevTimeNutrisi = t;

    x = getTds(-1);
    if (x < konfigPpm) { // jika nutrisinya kurang
      duration = K * (konfigPpm - x) / (P - konfigPpm);
      if (duration > 60) {
        duration = 60;
      }
      valve.on((int)duration);
    }
  }
}

long nextSchedule = 60; // sekedul pertama 1 menit setelah menyalah
void handleSchedule() {
  // scheduler
  if (nextSchedule < detik()) {
    if (!pompa1.state()) {
      pompa1On(konfig.duration);
    }
  }
}

void handleDisplay() {
  char txtInfo[][16] = {
    "TDS           ",
    "PPM           ",
    "DURASI (dtk)  ",
    "INTERVAL (mnt)",
    "KALIBRASI TDS "
  };
  int tds = appState.rawTds * konfig.tdsFactor;
  int vals[5] = {tds, konfig.ppm * 10, konfig.duration, konfig.periode, tds};
  char buf[6];

  if (appState.current != appState.last) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(txtInfo[appState.current]);
    appState.last = appState.current;
    appState.changed = true;
  }
  if (appState.changed) {
    snprintf(buf, 6, "%4d", vals[appState.current]);
    lcd.setCursor(12, 1);
    lcd.print(buf);
    appState.changed = false;
  }

  if (appState.lightOff > 0 && appState.lightOff < detik()) {
    lcd.noBacklight();
    appState.lightOff = 0;
    appState.current = 0;
  }
}

void handleAction(byte action) {
  int updown = 0;
  bool light = false;

  switch (action) {
    case acMenu:
      if (appState.lightOff > 0 && appState.last != 99) {
        appState.current = (appState.current + 1) % 5;
      }
      if (appState.current == 0) {
        getTds(1);
      }
      if (appState.nextSave > 0) {
        appState.nextSave = detik();
      }
      light = true;
      break;

    case acOk:
      light = true;
      pompa1.state() ? pompa1.off() : pompa1On(konfig.duration);
      break;

    case acDown:
    case acUp:
      updown = action == acDown ? -1 : 1;
      light = true;
      switch (appState.current) {
        case 0:
          getTds(1);
          break;

        case 1: // ppm
          if ((updown == -1 && konfig.ppm > PPM_MIN) || (updown == 1 && konfig.ppm < PPM_MAX)) {
            konfig.ppm += updown;
            appState.changed = true;
          }
          break;
        case 2: // durasi
          if ((updown == -1 && konfig.duration > DURATION_MIN) || (updown == 1 && konfig.duration < DURATION_MAX)) {
            konfig.duration += updown;
            appState.changed = true;
          }
          break;
        case 3: // periode
          if ((updown == -1 && konfig.periode > PERIODE_MIN) || (updown == 1 && konfig.periode < PERIODE_MAX)) {
            konfig.periode += updown;
            appState.changed = true;
          }
          break;
        case 4: // kalibrasi
          if ((updown == -1 && konfig.tdsFactor > 10) || (updown == 1 && konfig.tdsFactor < 4900)) {
            konfig.tdsFactor = updown * (1 + 1.01 * konfig.tdsFactor);
            appState.changed = true;
          }
        default:
          break;
      }
      if (appState.changed) {
        appState.nextSave = detik() + 5;
      }
      break;
  }
  if (light) {
    if (appState.lightOff == 0) {
      lcd.backlight();
    }
    appState.lightOff = detik() + 15;
  }
}


void handleSave() {
  if (appState.nextSave > 0 && appState.nextSave < detik()) {
    saveKonfig();
    appState.nextSave = 0;
  }
}

void saveKonfig() {
  EEPROM.put(0, konfig);

#if defined(IS_WEMOS)
  EEPROM.commit();
#endif
}

void eepromBegin() {
#if defined(IS_WEMOS)
  EEPROM.begin(64);
#endif
}

//* ******************************************************* */
//                     SERIAL FUNCTION
//* ******************************************************* */
String printLcd(String s) {
  lcd.home();
  lcd.print(s);
  return s;
}

String scanI2C(String s) {
  String r = "";
  int error, i, c = 0;
  for (i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    error = Wire.endTransmission();
    if (error == 0) {
      r += "I2C address 0x" + String(i, HEX) + " found\n";
      c++;
    } else if (error == 4) {
      r += "Unknown error 0x" + String(i, HEX) + "\n";
    }
  }
  if (c == 0) {

  }

  return r;
}
String getTimeNow(String s) {
  char formatted[32];
  snprintf(formatted, 32, "%d-%02d-%02d %02d:%02d:%02d %d", year(), month(), day(), hour(), minute(), second(), weekday());
  return String(formatted);
}

String samplingTds(String s) {
  int x = getTds(-1);
  return String(x);
}

String setPpm(String s) {
  int i = 0, inp = getIntFromStr(s, i) / 10;
  if (inp >= PPM_MIN && inp <= PPM_MAX) {
    konfig.ppm = inp;
    if (appState.current == 1) {
      appState.changed = true;
    }
    saveKonfig();
  }
  return String(konfig.ppm * 10);
}

String setPeriode(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= PERIODE_MIN && inp <= PERIODE_MAX) {
    konfig.periode = inp;
    if (appState.current == 2) {
      appState.changed = true;
    }
    saveKonfig();
  }
  return String(konfig.periode);
}

String setDurasi(String s) {
  int i = 0, inp = getIntFromStr(s, i);
  if (inp >= DURATION_MIN && inp <= DURATION_MAX) {
    konfig.duration = inp;
    if (appState.current == 3) {
      appState.changed = true;
    }
    saveKonfig();
  }
  return String(konfig.duration);
}

String setPompaOn(String s) {
  int i = 0, inp;
  if (s.length() == 0) {
    inp = konfig.duration;
  } else {
    inp = getIntFromStr(s, i);
  }
  if (inp < 15) {
    inp = 15;
  } else if (inp > 300) {
    inp = 300;
  }
  pompa1On(inp);
  return "ON " + String(inp) + " detik";
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

float getRawTds() {
  // konstanta diperoleh dari percobaan. Hasil dari fungsi ini dibandingkan dengan tds meter standar. Hasilnya diregresikan
  // Baca README.md
  float x1, x2;

  /* dari pengalaman. kalau probe yang dicelupkan ke air diberi arus searah terus menerus akan terjadi elektrolisis
      sehinggah nilai pengukuran akan berubah.
      Karena itu sebelum pensamplingan nilai, probe terlebih dahulu diberi arus bolak-balik
  */
  //init
  digitalWrite(TDS_SOURCE_1, LOW);

  // sampling 1 -> PIN1 high, PIN2 low
  digitalWrite(TDS_SOURCE_2, LOW);
  digitalWrite(TDS_SOURCE_1, HIGH);
  x1 = analogRead(A0) / 1023.0;

  // sampling 2 -> PIN1 low, PIN2 high
  digitalWrite(TDS_SOURCE_2, HIGH);
  digitalWrite(TDS_SOURCE_1, LOW);
  x2 = analogRead(A0) / 1023.0;
  // idle
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  // konversi ke ec
  return x1 / (1 - x1) + (1 - x2) / x2;
}

int getTds() {
  return getTds(0);
}

long nextReadTds = 0;
int getTds(int t) {
  int c = (NEXT_READ_TDS - 30) * t;
  if (t < 0 || (nextReadTds - c) < detik()) {
    appState.rawTds = getRawTds();
    nextReadTds = detik() + NEXT_READ_TDS;
    appState.changed = appState.changed || appState.current == 0;
  }
  return (int) (konfig.tdsFactor * appState.rawTds);
}

void pompa1On(int duration) {
  nextNutrisi = detik() + duration; // tambah nutrisi setelah pompa mati
  if (year() < 2017 || (hour() > 6 && hour() < 18)) {
    nextSchedule = detik() + konfig.periode * 60;
  } else {
    nextSchedule = detik() + 2 * konfig.periode * 60; // kalau malam
  }
  pompa1.on(duration);
}


