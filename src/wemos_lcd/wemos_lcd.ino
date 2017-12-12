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

#if defined(ARDUINO_ESP8266_WEMOS_D1MINI)    // wemos
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#define TDS_SOURCE_1 D0
#define TDS_SOURCE_2 D3
#define RELAY_VALVE D5
#define RELAY_POMPA1 D6
#define PUSH_BUTTON D4
#define RECV_PIN D7
#define CONST_A -15.0      //  konstanta regresi tds
#define CONST_B 310.0      //  Y = CONST_A + CONST_B * ec
#define IS_WEMOS true

#else // arduino
#include <IRremote.h>

#define TDS_SOURCE_1 2
#define TDS_SOURCE_2 5
#define RELAY_VALVE 7
#define RELAY_POMPA1 8
#define PUSH_BUTTON 6
#define RECV_PIN 11
#define CONST_A -10.0      //  konstanta regresi tds
#define CONST_B 500.0      //  Y = CONST_A + CONST_B * ec
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

class TRelay {
  private:
    byte _pin;
    long _after;
    long _until;
  public:
    TRelay(byte pin) {
      _pin = pin;
      _after = 0;
      _until = 0;
      pinMode(_pin, OUTPUT);
    }
    void run() {
      if (_after > 0 && _after < detik()) {
        _after = 0;
        digitalWrite(_pin, HIGH);
      }
      if (_until > 0 && _until < detik()) {
        _until = 0;
        digitalWrite(_pin, LOW);
      }
    }

    void on(unsigned int durasi, unsigned int after = 0) {
      if (after == 0) {
        _after = 0;
        digitalWrite(_pin, HIGH);
      } else {
        _after = detik() + after;
      }
      _until = (durasi > 0) ? detik() + (after + durasi) : 0;
    }

    void off() {
      _until = 0;
      _after = 0;
      digitalWrite(_pin, LOW);
    }

    boolean state() {
      return digitalRead(_pin);
    }

    String status() {
      String s = "";
      long t;
      if (digitalRead(_pin)) {
        s += "ON";
        t = _until - detik();
      } else {
        s += "OFF";
        t = _after - detik();
      }
      if (t > 0) {
        s += " -> " + String(t / 60) + ":" + String(t % 60);
      }
      return s;
    }
};

/* *************************************************** */
//              GLOBAL VAR
/* *************************************************** */

struct TConfig {
  int ppm;
  int periode; // dalam menit
  int duration; // dalam detik
};
struct TAppState {
  byte current, last;
  int
  //ppm, duration, periode,
  tds;
  long nextSave, lightOff;
  bool changed;
};

TAppState appState;
Button button(PUSH_BUTTON, BUTTON_PULLUP);

TProcess process;
TConfig konfig;
TRelay valve(RELAY_VALVE), pompa1(RELAY_POMPA1);

IRrecv irrecv(RECV_PIN);
decode_results results;
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* ******************************************************* */
//                     MAIN FUNCTION
/* ******************************************************* */
void setup(void) {
  int i;
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
  process.addCommand("analog", readAnalog);
  process.addCommand("on", setPompaOn);
  process.addCommand("status", getStatusPompa);

  // read config
  // ppm
  EEPROM.get(0, k);
  konfig.ppm = (k.ppm >= PPM_MIN && k.ppm <= PPM_MAX) ? k.ppm : 100; // default 1000
  konfig.periode = (k.periode >= PERIODE_MIN && k.periode <= PERIODE_MAX) ? k.periode : 60; // default 60 menit
  konfig.duration = (k.duration >= DURATION_MIN && k.duration <= DURATION_MAX) ? k.duration : 20; // default 20 detik

  irrecv.enableIRIn();
  lcd.begin();

  appState.current = 0;
  appState.last = 99;
}

/**
   Main process
*/
void loop(void) {
  process.processSerial();
  handleReadTds();
  handleDisplay();
  handleRemote();
  handleSave();
  handlePushButton();
  handleSchedule();
  handleRelays();
  handleNutrisi();
}


//* ******************************************************* */
//                     LOOP FUNCTION
//* ******************************************************* */

long nextReadTds = 0;
int readTds() {
  appState.tds = getTds();
  nextReadTds = detik() + NEXT_READ_TDS;
  appState.changed = appState.changed || appState.current == 0;
  return appState.tds;
}

void handlePushButton() {
  if (button.isPressed() && button.held(50)) {
    if (pompa1.state()) {
      pompa1.off();
      Serial.println("OFF");
    } else {
      pompa1On(konfig.duration);
      Serial.println("ON");
    }
  }
}

void handleRelays() {
  valve.run();
  pompa1.run();
}

void handleReadTds() {
  if (nextReadTds < detik()) {
    readTds();
  }
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

    x = readTds();
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
  char txtInfo[][10] = {"TDS     ", "PPM     ", "DURATION", "PERIODE "};
  int vals[4] = {appState.tds, konfig.ppm * 10, konfig.duration, konfig.periode};
  char buf[6];

  if (appState.current != appState.last) {
    lcd.setCursor(5, 0);
    lcd.print(txtInfo[appState.current]);
    appState.last = appState.current;
    appState.changed = true;
  }
  if (appState.changed) {
    snprintf(buf, 6, "%4d", vals[appState.current]);
    lcd.setCursor(10, 1);
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
      if (appState.lightOff > 0) {
        appState.current = (appState.current + 1) % 4;
      }
      if (appState.nextSave > 0) {
        appState.nextSave = detik();
      }
      light = true;
      break;

    case acDown:
    case acUp:
      updown = action == acDown ? -1 : 1;
      light = true;
      switch (appState.current) {
        case 0:
          nextReadTds = 0;
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
        default:
          break;
      }
      if (appState.changed) {
        appState.nextSave = detik() + 5;
      }
      break;

    case acOk:
      light = true;
      pompa1.state() ? pompa1.off() : pompa1On(konfig.duration);
      break;
  }
  if (light) {
    if (appState.lightOff == 0) {
      lcd.backlight();
    }
    appState.lightOff = detik() + 60;
  }
}

void handleRemote() {
  if (irrecv.decode(&results))
  {
    switch (results.value) {
      case IR_MENU:
        handleAction(acMenu);
        break;

      case IR_UP:
        handleAction(acUp);
        break;

      case IR_DOWN:
        handleAction(acDown);
        break;

      case IR_OK:
        handleAction(acOk);
        break;

      default:
#if defined(IS_WEMOS)
        serialPrintUint64(results.value, HEX);
#else
        Serial.print(results.value, HEX);
#endif
        Serial.println("");
        break;
    }
    irrecv.resume(); // Receive the next value
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
String getTimeNow(String s) {
  char formatted[32];
  snprintf(formatted, 32, "%d-%02d-%02d %02d:%02d:%02d %d", year(), month(), day(), hour(), minute(), second(), weekday());
  return String(formatted);
}

String samplingTds(String s) {
  int x = readTds();
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

String readAnalog(String s) {
  return String(analogRead(A0));
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

String getStatusPompa(String s) {
  long t = nextSchedule - detik();
  String r = "";
  if (pompa1.state()) {
    r += "POMPA " + pompa1.status();
  } else {
    r += "POMPA OFF";
    if (t > 0) {
      r += " -> ";
      r += String(t / 60) + ":" + String(t % 60);
    }
  }
  return r;
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

int getTds() {
  // konstanta diperoleh dari percobaan. Hasil dari fungsi ini dibandingkan dengan tds meter standar. Hasilnya diregresikan
  // Baca README.md
  float x1, x2, ec, tds;

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
  ec = x1 / (1 - x1) + (1 - x2) / x2;
  tds = CONST_A + CONST_B * ec;
  return tds > 0 ? (int)tds : 0;
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


