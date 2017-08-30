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
*/
#include <RealTimeClockDS1307.h>
#include <EEPROM.h>
#include <Keypad.h>

//RealTimeClock RTC;//=new RealTimeClock();

#define SET_TIME 't'
#define GET_TIME_NOW 'n'
#define SWITCH_ON '1'
#define SWITCH_OFF '0'
#define SWITCH_TOGGLE 'x'
#define ADD_TIMER 'a'
#define EDIT_TIMER 'e'
#define DELETE_TIMER 'd'
#define LIST_TIMERS 'l'
#define STATE_PIN 's'
#define STATE_PIN_DETAIL 'S'
#define HOLD_KEY_TIME 3 // tahan 3 detik untuk menyalakan terus

#define TDS_SOURCE_1 2
#define TDS_SOURCE_2 3
#define TDS_INPUT A0


const byte TIMER_VALID = B1001;
int count = 0;
byte outputPins[] = {10, 11, 12, 13}; // pin ke relay
int timerDurations[] = {0, 0, 0, 0, 0, 0, 0, 0};
byte TIMER_COUNT, TIMER_SIZE;
char lastKey = NO_KEY;
int holdTime = 0;
char formatted[] = "00-00-00 00:00:00x";

// KEYPAD
const byte numRows = 4;
const byte numCols = 4;
char keymap[numRows][numCols] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[numRows] = {9, 8, 7, 6}; //Rows 0 to 3
byte colPins[numCols] = {5, 4, 3, 2}; //Columns 0 to 3

//initializes an instance of the Keypad class

//Keypad myKeypad = Keypad(makeKeymap(keymap), rowPins, colPins, numRows, numCols);

struct TTimer {
  byte config;
  byte y;
  byte m;
  byte d;
  byte h;
  byte i;
  unsigned int durasi;
};

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

int getEepromAddress(int idx) {
  TTimer obj;
  int c = 0, x;
  for (x = 0; x < TIMER_COUNT; x++) {
    EEPROM.get(x * TIMER_SIZE, obj);
    if (obj.config >> 4 == TIMER_VALID) { // valid
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

// baca inputan dari serial.
void processCommand() {
  if (!Serial.available()) {
    return;
  }
  int in, durasi, idx, dtkSisa;
  int first = -1, ii = 0, idxFound = -1;
  byte pin, x;
  boolean state;
  char c;
  TTimer obj;
  float tds;

  char command = Serial.read();
  switch (command) {
    case GET_TIME_NOW:
      RTC.readClock();
      RTC.getFormatted(formatted);
      Serial.print(formatted);
      Serial.print(' ');
      Serial.print(RTC.getDayOfWeek());
      Serial.println();
      break;

    // set waktu YYMMDD-HHIISS
    case SET_TIME:
      in = readSerialIntN(2);
      RTC.setYear(in);
      in = readSerialIntN(2);
      RTC.setMonth(in);
      in = readSerialIntN(2);
      RTC.setDate(in);
      Serial.read(); // separator
      in = readSerialIntN(2);
      RTC.setHours(in);
      in = readSerialIntN(2);
      RTC.setMinutes(in);
      in = readSerialIntN(2);
      RTC.setSeconds(in);
      if (Serial.available()) {
        Serial.read(); // sparator
        in = readSerialIntN(1);
        RTC.setDayOfWeek(in);
      }
      RTC.setClock();
      Serial.println("Done");
      break;

    // set pin output
    case SWITCH_ON: // on
    case SWITCH_OFF: // off
    case SWITCH_TOGGLE: // toggle
      c = Serial.read();
      if (c >= '0' && c <= '7') {
        x = c - '0';
        pin = outputPins[x];
        if (command == SWITCH_ON) {
          state = HIGH;
        } else if (command == SWITCH_OFF) {
          state = LOW;
        } else {
          state = !digitalRead(pin); // toggle
        }
        Serial.read(); // spasi
        durasi = readSerialIntN(5);
        setStatePin(x, state, durasi);
      }
      break;

    // tambah timer.
    case ADD_TIMER:
      idx = getEepromAddress(-1);
      if (idx >= 0 && setTimer(idx)) {
        Serial.println("Success");
      } else {
        Serial.println("Fail");
      }
      break;

    // edit timer
    case EDIT_TIMER:
      idx = readSerialIntN(3); // address
      idx = getEepromAddress(idx);
      if (idx >= 0 && setTimer(idx)) {
        Serial.println("Success");
      } else {
        Serial.println("Fail");
      }
      break;

    case DELETE_TIMER:
      idx = readSerialIntN(3); // address
      idx = getEepromAddress(idx);
      if (idx >= 0) {
        obj.config = 0;
        EEPROM.put(idx * TIMER_SIZE, obj);
        Serial.println("Success");
      } else {
        Serial.println("Fail");
      }
      break;

    case LIST_TIMERS:
      getTimers();
      break;

    case STATE_PIN:
      for (x = 0; x < sizeof(outputPins); x++) {
        if (digitalRead(outputPins[x])) {
          Serial.print('1');
        } else {
          Serial.print('0');
        }
      }
      Serial.println();
      break;

    case STATE_PIN_DETAIL:
      dtkSisa = 60 - (count % 60);
      for (x = 0; x < sizeof(outputPins); x++) {
        if (digitalRead(outputPins[x])) {
          Serial.print(x);
          Serial.print(" ON ");
          if (timerDurations[x] > 0) {
            Serial.print(timerDurations[x] - 1);
            Serial.print(':');
            Serial.print(dtkSisa);
          }
        } else {
          Serial.print(x);
          Serial.print(" OFF");
        }
        Serial.println();
      }
      break;

    case 'c':
      tds = samplingTds();
      Serial.println(tds);
      break;

    default:
      Serial.readString();
      Serial.print("Unknown command: ");
      Serial.print(command);
      Serial.println();
      break;
  }
}

void processKeypad() {
  char key;
  byte x, i;
  boolean state;

  for (i = 0; i < 5; i++) {
    //key = myKeypad.getKey();
    if (key != NO_KEY) {
      break;
    } else {
      lastKey = NO_KEY;
      delay(200);
    }
  }
  if (key != NO_KEY && key != lastKey) {
    if (key >= '1' && key <= '8') {
      x = key - '1';
      state = !digitalRead(outputPins[x]);
      setStatePin(x, state, 5);
    }
  }
  if (lastKey == key) {
    holdTime++;
    if (holdTime > HOLD_KEY_TIME && key >= '1' && key <= '8') { // jika tombol ditahan 3 detik
      x = key - '1';
      if (digitalRead(outputPins[x])) { // jika kondisi sedang ON
        setStatePin(x, true, 0);
        holdTime = 0;
      }
    }
  } else {
    holdTime = 0;
  }
  lastKey = key;
  delay(200 * (5 - i));
}

int readSerialIntN(int l) {
  int i = 0;
  boolean done = false;
  while (Serial.available() && !done && l > 0)
  {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      i = i * 10 + (c - '0');
    } else {
      done = true;
    }
    l--;
  }
  return i;
}

int readSerialIntX(int max) {
  int i = 0;
  boolean done = false;
  while (Serial.available() && !done)
  {
    char c = Serial.read();
    if (i == 0 && c == '*') {
      Serial.read(); // spasi
      return 255;
    }
    if (c >= '0' && c <= '9') {
      i = i * 10 + (c - '0');
    } else {
      done = true;
    }
  }
  return i <= max ? i : -1;
}

void processTimer() {
  // read eeprom
  byte index, x, objDayOfWeek;
  unsigned int durasi;
  boolean match, action;
  TTimer obj;
  RTC.readClock();
  byte y = RTC.getYear();
  byte m = RTC.getMonth();
  byte d = RTC.getDate();
  byte h = RTC.getHours();
  byte i = RTC.getMinutes(); // minute
  byte w = RTC.getDayOfWeek();

  for (index = 0; index < TIMER_COUNT; index++) {
    EEPROM.get(index * TIMER_SIZE, obj);
    if (obj.config >> 4 == TIMER_VALID) { // valid
      action = (obj.config & B00001000) == B00001000; // ON atau OFF
      x = obj.config & B00000111; // nomor pin
      // day of week disimpan bersama durasi sebagai bit ke 12-15. Range nilai 0 - 6
      objDayOfWeek = (obj.durasi >> 13) + 1;

      // cocokkan konfig
      match = (obj.y >= 100 || obj.y == y) &&
              (obj.m > 12 || obj.m == m) &&
              (obj.d > 31 || obj.d == d) &&
              (obj.h > 23 || obj.h == h) &&
              (obj.i > 59 || obj.i == i) &&
              (objDayOfWeek > 7 || objDayOfWeek == w);
      if (match) {
        // jika timer cocok, set state yang sesuai
        durasi = (obj.durasi << 3) >> 3;
        setStatePin(x, action, durasi);
      }
    }
  }
}

boolean setTimer(byte address) {
  TTimer obj;
  char c;
  int in;
  if (Serial.available()) { // action
    obj.config = TIMER_VALID << 4;
    c = Serial.read();
    if (c == '1') { // jika action == 1 => ON
      obj.config = obj.config + B1000;
    }
  } else {
    return false;
  }

  if (Serial.available()) { // pin
    c = Serial.read();
    if (c >= '0' && c <= '7') {
      obj.config = obj.config + (c - '0');
    } else {
      return false;
    }
  } else {
    return false;
  }
  Serial.read(); // spasi

  in = readSerialIntX(99); // year
  if (in >= 0) {
    obj.y = in;
  } else {
    return false;
  }
  in = readSerialIntX(12); // month
  if (in >= 0) {
    obj.m = in;
  } else {
    return false;
  }
  in = readSerialIntX(31); // date
  if (in >= 1) {
    obj.d = in;
  } else {
    return false;
  }
  in = readSerialIntX(23); // hour
  if (in >= 0) {
    obj.h = in;
  } else {
    return false;
  }
  in = readSerialIntX(59); // minute
  if (in >= 0) {
    obj.i = in;
  } else {
    return false;
  }

  if (Serial.available()) { // day of week
    c = Serial.read();
    if (c >= '1' && c <= '7') {
      obj.durasi = (c - '1') << 13;
    } else if (c == '*') {
      obj.durasi = 7 << 13;
    } else {
      return false;
    }
  }

  Serial.read(); // spasi
  in = readSerialIntN(4); // durasi -> 4 digit max 8191
  if (in <= 8191) {
    obj.durasi = obj.durasi + in;
  }

  // tambahkan ke eeprom
  EEPROM.put(address * TIMER_SIZE, obj);
  return true;
}

void getTimers() {
  byte index, x, n = 0;
  boolean action;
  TTimer obj;
  unsigned int durasi, objDayOfWeek;

  for (index = 0; index < TIMER_COUNT; index++) {
    EEPROM.get(index * TIMER_SIZE, obj);
    if (obj.config >> 4 == TIMER_VALID) { // valid
      n++;
      action = (obj.config & B00001000) == B00001000; // ON atau OFF
      if (action) {
        Serial.print("ON ");
      } else {
        Serial.print("OFF ");
      }
      x = obj.config & B00000111; // nomor pin
      Serial.print(x);
      Serial.print(' ');

      objDayOfWeek = (obj.durasi >> 13) + 1; // day of week disimpan bersama durasi sebagai bit ke 13-15
      durasi = (obj.durasi << 3) >> 3;

      if (obj.y >= 100) { // tahun
        Serial.print('*');
      } else {
        Serial.print(obj.y);
      }
      Serial.print('-');

      if (obj.m > 12) { // bulan
        Serial.print('*');
      } else {
        Serial.print(obj.m);
      }
      Serial.print('-');

      if (obj.d > 31) { // tanggal
        Serial.print('*');
      } else {
        Serial.print(obj.d);
      }
      Serial.print(' ');

      if (obj.h >= 24) { // jam
        Serial.print('*');
      } else {
        Serial.print(obj.h);
      }
      Serial.print(':');
      if (obj.i >= 60) { // menit
        Serial.print('*');
      } else {
        Serial.print(obj.i);
      }
      Serial.print(' ');

      if (objDayOfWeek > 7) { // dayofweek
        Serial.print('*');
      } else {
        Serial.print(objDayOfWeek);
      }
      Serial.print(' ');
      Serial.print(durasi);
      Serial.println();
    }
  }
  Serial.print("Timer available: ");
  Serial.print(TIMER_COUNT - n);
  Serial.println();
}



float samplingTds() {
  float x1, x2, r;
  float C1 = 0.0, C2 = 1000.0;
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

  r = (x1 + 1) / (1024 - x1) + (1024 - x2) / (x2 + 1);
  return C1 + C2 / r;
}

void setup() {
  Serial.begin(115200);

  // set mode pin (2 - 9) sebagai output
  byte i;
  for (i = 0; i < sizeof(outputPins); i++) {
    pinMode(outputPins[i], OUTPUT);
  }

  pinMode(TDS_SOURCE_1, OUTPUT);
  pinMode(TDS_SOURCE_2, OUTPUT);

  // kalibrasi waktu
  RTC.readClock();
  count = RTC.getSeconds();

  TIMER_SIZE = sizeof(TTimer);
  TIMER_COUNT = EEPROM.length() / TIMER_SIZE;
}

void loop() {
  byte i;
  processCommand();
  //processKeypad();

  if (count % 60 == 0) {
    // matikan state yg durasinya sudah habis
    for (i = 0; i < sizeof(outputPins); i++) {
      if (timerDurations[i] > 0) {
        timerDurations[i]--;
        if (timerDurations[i] == 0) {
          digitalWrite(outputPins[i], LOW);
          Serial.print("OFF ");
          Serial.println(i);
        }
      }
    }
    processTimer();
  }
  if (count >= 3600) { // kalibarsi detik
    RTC.readClock();
    count = RTC.getSeconds();
  }
  count++;
}

