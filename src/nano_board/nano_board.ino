/**
   Timer dengan interface serial
   # Untuk mengatur waktu adalah dengan mengirim perintah
       tYYMMDD-HHIISS[-W] --> Contoh t170809-073426 mengatur waktu ke 09/08/2017 07:34:26
                              W adalah day of week (opsional). Contoh dengan set tanggal dengan day of week t170812-073426-6

   # Untuk mendapatkan waktu sekarang adalah dengan mengirim perintah n.

   # Untuk menyalahkan/mematikan pin (1|0|x)(0-7) [DURASI]. Contoh:
       - 15 100 : menyalahkan pin 5 selama 100 menit
       - 05 --> : mematikan pin 5
       - x2 [5] --> Menukar state dari pin 2. Jika state sekarang adalah ON, durasi 5 menit akan diterapkan.

   # Menambah atau mengubah timer.
     -Menambah timer.
       a(1|0)(0-7) YY-MM-DD HH:II W [DURASI] --> Digit pertama adalah action ('0' atau '1').
                                Digit kedua adalah no pin (0 - 7).
                                YY-MM-DD HH:II W adalah tahun, bulan, tanggal, menit serta hari ke dalam seminggu.
                                Contoh a15 * * * 7 00 * 10 -> nyalakan relay 5 pada jam 7 menit ke 0 setiap hari(bulan dan tahun) selama 10 menit.

     -Update timer
       eA:(1|0)(0-7) YY-MM-DD HH:II W [DURASI] --> A adalah index timer yang akan diedit. Lihat bagian "Menambah timer".
                               Contoh e3:15 * * * 7 00 1 5 -> nyalakan setiap Senin jam 7:00 selama 5 menit.

     -Delete timer
       dA                       --> A adalah index timer. Contoh d15 -> delete timer ke 15.

   # Untuk mendapatkan daftar timer adalah dengan mengirim perintah l.

   # Untuk melihat state pin menggunakan perintah s. Untuk detail state perintahnya adalah S.


  SD card datalogger

  This example shows how to log data from three analog sensors
  to an SD card using the SD library.

  The circuit:
   analog sensors on analog ins 0, 1, and 2
   SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10 (for MKRZero SD: SDCARD_SS_PIN)

  created  24 Nov 2010
  modified 9 Apr 2012
  by Tom Igoe

  This example code is in the public domain.

*/


#include <RealTimeClockDS1307.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>

#define TIMER_VALID B1001
#define TDS_SOURCE_1 2
#define TDS_SOURCE_2 3
#define TDS_INPUT A0
#define TIMER_ADDRESS 64
#define POMPA 0
#define VALVE 1


struct TTimerConfig {
  byte y;
  byte m;
  byte d;
  byte h;
  byte i;
  byte w;
  unsigned int action;
};

struct {
  byte y;
  byte m;
  byte d;
  byte h;
  byte i;
  byte w;
} currentTime;

/* ***************************************** */
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


/* ***************************************************** */
//                 CLASS DEFINITION
/* ***************************************************** */
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
    void (*_customProcess)(String);

  public:
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
      String s = Serial.readString(), cmd, content;
      int i = s.indexOf(':');
      TSerialCommand *sc = _first;
      if (i > 0) {
        cmd = s.substring(0, i - 1);
        content = s.substring(i + 1);
        while (sc) {
          if (sc->process(cmd, content)) {
            return;
          } else {
            sc = sc->next();
          }
        }
      }
      if (_customProcess) {
        _customProcess(s);
      }
    }
};


// ********************* EepromTimer ***********************
class TEepromTimer {
  private:
    int _beginAddress;
    int _count;
    int _timerSize;
    int _length;
    void (*_callback)(unsigned int);

    int getTimerAddress(int idx) {
      TTimerConfig obj;
      int c = 0, x;
      for (x = _beginAddress; x < _length; x += _timerSize) {
        EEPROM.get(x, obj);
        if (obj.w >> 4 == TIMER_VALID) { // valid
          if (idx == c) {
            return x;
          }
          c++;
        } else if (idx < 0) {
          return x;
        }
      }
      return -1;
    }

  public:
    TEepromTimer(void (*callback)(unsigned int)) {
      TEepromTimer(0, callback);
    }

    TEepromTimer(int beginAddress, void (*callback)(unsigned int)) {
      _beginAddress = beginAddress;
      _timerSize = sizeof(TTimerConfig);
      _length = EEPROM.length();
      _count = (_length - _beginAddress) / _timerSize;
      _callback = callback;
    }

    boolean addTimer(String ss, unsigned int action) {
      setTimer(ss, action, -1);
    }

    boolean setTimer(String ss, unsigned int action, int idx) {
      int i = 0, inp;
      idx = getTimerAddress(idx);
      TTimerConfig obj;
      if (idx >= 0) {
        obj.action = action;
        obj.w = TIMER_VALID << 4;

        // year
        if (ss[i] == '*') {
          obj.y = 255;
          i += 2;
        } else {
          inp = getIntFromStr(ss, i);
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
          inp = getIntFromStr(ss, i);
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
          inp = getIntFromStr(ss, i);
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
          inp = getIntFromStr(ss, i);
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
          inp = getIntFromStr(ss, i);
          i++;
          if (inp >= 0 && inp <= 59) {
            obj.i = inp;
          } else {
            return false;
          }
        }
        // day of week
        if (ss[i] == '*') {
          obj.w += 15;
        } else if (ss[i] >= '1' && ss[i] <= '7') {
          obj.w += (ss[i] - '0');
        } else {
          return false;
        }

        // save to eeprom
        EEPROM.put(idx, obj);
        //EEPROM.commit();
        return true;
      }
      return false;
    }

    boolean deleteTimer(int idx) {
      idx = getTimerAddress(idx);
      TTimerConfig obj;
      if (idx >= 0) {
        obj.w = 0;
        EEPROM.put(idx, obj);
        //EEPROM.commit();
        return true;
      }
      return false;
    }

    String _formatTimer(TTimerConfig obj) {
      String r = "";
      byte w = obj.w % 16;
      // show timer action
      byte action = obj.action >> 8,
           duration = obj.action % 256;

      r += (action >> 3) ? " ON " : " OFF ";
      r += String(action % 8);
      // duration
      if (duration > 0) {
        r += " " + String(duration);
      }
      r += "=>";
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
      r += (w == 15) ? "*" : String(w); // day of week

      return r;
    }

    String showTimer(int idx) {
      String s = "";
      TTimerConfig obj;
      int c = 0, x;
      for (x = _beginAddress; x < _length; x += _timerSize) {
        EEPROM.get(x, obj);
        if (obj.w >> 4 == TIMER_VALID) { // valid
          if (idx == c) {
            return _formatTimer(obj);
          } else if (idx < 0) {
            s += _formatTimer(obj) + "\n";
          }
          c++;
        }
      }
      s += "Available: " + String(_count - c);
      return s;
    }

    void processAll() {
      TTimerConfig obj;
      int x;
      byte w;
      boolean match;
      if (!_callback) {
        return;
      }
      for (x = _beginAddress; x < _length; x += _timerSize) {
        EEPROM.get(x, obj);
        if (obj.w >> 4 == TIMER_VALID) { // valid
          w = obj.w % 16;
          match = (obj.y == 255 || obj.y == currentTime.y) &&
                  (obj.m == 255 || obj.m == currentTime.m) &&
                  (obj.d == 255 || obj.d == currentTime.d) &&
                  (obj.h == 255 || obj.h == currentTime.h) &&
                  (obj.i == 255 || obj.i == currentTime.i) &&
                  (w == 15 || w == currentTime.w);

          if (match) {
            _callback(obj.action);
          }
        }
      }
    }
};

/* ****************************************** */
//                 Global Var
/* ****************************************** */
TEepromTimer eepromTimer(TIMER_ADDRESS, processTimer);
TSerialProcess serialProcess;

/**
 * Konfigurasi pin/relay
 * 5 -> pompa 
 * 6 -> seleoid valve
 * 
 * Untuk PULL PUSH tds sensor menggunakan pin 2 dan 3
 */
byte outputPins[] = {5, 6, 7, 8}; // pin ke relay
long timerDurations[] = {0, 0, 0, 0};
byte PIN_COUNT;
char formatted[] = "00-00-00 00:00:00x";
int ppmNutrisi = 1000;

long milis, lastMilis = 0;

/* ****************************************** */
//                 Functions
/* ****************************************** */


void processTimer(unsigned int action) {
  byte a = action >> 8, duration = action % 256;
  setStatePin(a % 8, a >> 3, duration * 60);
}

void setStatePin(byte x, boolean state, int durasi) {
  digitalWrite(outputPins[x], state);
  if (state) {
    if (durasi > timerDurations[x] || durasi == 0) {
      timerDurations[x] = durasi;
    }
    Serial.print("ON ");
  } else {
    timerDurations[x] = 0;
    Serial.print("OFF ");
  }
  Serial.println(x);
}

/*
   Send current time to serial port
*/
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

void switchPin(String s) {
  char action = s[0], c = s[1];
  byte x;
  int inp, i = 3, durasi = 0;
  boolean state;
  if (c >= '0' && c <= '7') {
    x = c - '0';
    state = (action == '1') || (action == 'x' && !digitalRead(outputPins[x]));
    if (i < s.length() - 1) {
      durasi = getIntFromStr(s, i);
    }
    setStatePin(x, state, durasi * 60);
  }
}

/*
    AP [DURATION]=>Y M D H I W
*/
void addTimer(String s) {
  int sp = s.indexOf("=>"), i;
  byte action, duration = 0;
  unsigned int u;
  String s1, s2;
  if (sp > 0) {
    s1 = s.substring(0, sp - 1);
    s2 = s.substring(sp + 2);
    s1.trim();
    s2.trim();

    // action timer adalah s1 "AP [DURATION]"
    action = (s1[0] == '1') ? 8 : 0; // action
    action += (s1[1] - '0'); // pin
    i = 3;
    if (i < s1.length() - 1) {
      duration = getIntFromStr(s1, i);
    }
    eepromTimer.addTimer(s2, action * 256 + duration);
  }
}

/*
    address:AP [DURATION]=>Y M D H I W
*/
void editTimer(String s) {
  int sp = s.indexOf("=>"), i, idx;
  byte action, duration = 0;
  unsigned int u;
  String s1, s2, sa;
  if (sp > 0) {
    s1 = s.substring(0, sp - 1);
    s2 = s.substring(sp + 2);
    s1.trim();
    s2.trim();

    // action timer adalah s1 "addres:AP [DURATION]"
    i = 0;
    idx = getIntFromStr(s1, i);
    i++;

    action = (s1[i] == '1') ? 8 : 0; // action
    action += (s1[i + 1] - '0'); // pin
    i += 3;
    if (i < s1.length() - 1) {
      duration = getIntFromStr(s1, i);
    }
    eepromTimer.setTimer(s2, action * 256 + duration, idx);
  }
}

void deleteTimer(String s) {
  int i = 0, idx;
  idx = getIntFromStr(s, i);
  eepromTimer.deleteTimer(idx);
}

void showTimer(String s) {
  int i = 0, idx;
  if (s.length() == 0) {
    Serial.println(eepromTimer.showTimer(-1));
  } else {
    idx = getIntFromStr(s, i);
    Serial.println(eepromTimer.showTimer(idx));
  }
}

void getCurrentTime() {
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
  float C1 = 35.5, C2 = 100.85; // konstanta dari percobaan
  // sampling 1 -> 1+, 2-
  digitalWrite(TDS_SOURCE_1, HIGH);
  digitalWrite(TDS_SOURCE_2, LOW);
  x1 = analogRead(TDS_INPUT);

  // sampling 2 -> 1-, 2+
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, HIGH);
  x2 = analogRead(TDS_INPUT);

  // idle
  digitalWrite(TDS_SOURCE_1, LOW);
  digitalWrite(TDS_SOURCE_2, LOW);

  r = x1 / (1023 - x1) + (1023 - x2) / x2;
  return C1 + C2 / r;
}

void tds(String s) {
  float x = samplingTds();
  Serial.println(x);
}

void setPPM(String s){
  unsigned int inp;
  int i=0;
  inp = getIntFromStr(s, i);
  if(inp >= 500 && inp <= 3000){
    ppmNutrisi = inp;
    inp += TIMER_VALID << 12;
    EEPROM.put(0, inp);
  }
}

void tambahNutrisiOtomatis(){
  int x = (int) samplingTds();
  int K = 10; // --> didapat dari percobaan
  if(x < ppmNutrisi){
    setStatePin(VALVE, true, (ppmNutrisi - x) * K);
  }
}

/* ****************************************** */
//                 Main Functions
/* ****************************************** */

void setup() {
  byte i;
  unsigned int inp;
  
  PIN_COUNT = sizeof(outputPins);
  Serial.begin(115200);

  // set mode sebagai output
  for (i = 0; i < PIN_COUNT; i++) {
    pinMode(outputPins[i], OUTPUT);
  }

  // ambil konfig ppm dari eeprom
  EEPROM.get(0, inp);
  if(inp >> 12 == TIMER_VALID){
    ppmNutrisi = (inp << 4) >> 4;
  }

  // serial process
  serialProcess.addCommand("now", getTimeNow);
  serialProcess.addCommand("set_time", setTimeNow);
  serialProcess.addCommand("switch", switchPin);
  serialProcess.addCommand("add_timer", addTimer);
  serialProcess.addCommand("edit_timer", editTimer);
  serialProcess.addCommand("delete_timer", deleteTimer);
  serialProcess.addCommand("show_timer", showTimer);
  serialProcess.addCommand("tds", tds);
  serialProcess.addCommand("set_ppm", setPPM);

  // kalibrasi waktu
  currentTime.y = 255;
  currentTime.m = 255;
  currentTime.d = 255;
  currentTime.h = 0;
  currentTime.i = 0;
  currentTime.w = 255;
  getCurrentTime();
}

void loop() {
  byte x;
  milis = millis() / 1000;
  serialProcess.processCommand();

  if (milis != lastMilis) { // ganti detik
    lastMilis = milis;
    for (x = 0; x < PIN_COUNT; x++) {
      if (timerDurations[x] > 0) {
        timerDurations[x]--;
        if (timerDurations[x] == 0) {
          digitalWrite(outputPins[x], LOW);
        }
      }
    }
    if (milis % 60 == 0) { // ganti menit
      getCurrentTime();
      eepromTimer.processAll();

      // hard coded timer
      // POMPA tiap 1 jam nyala selama 3 menit
      if(currentTime.i == 0){
        setStatePin(POMPA, true, 3*60);
      }
      // cek nutrisi setelah selesai pengairan
      if(currentTime.i == 4){
        tambahNutrisiOtomatis();
      }
    }
  }

  delay(10);
}

