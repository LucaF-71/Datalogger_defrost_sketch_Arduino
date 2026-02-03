#include "RTClib.h"
#include <SD.h>
#include <EEPROM.h>
#include <SPI.h>  // necessaria per SD

// Pin e indirizzi
#define PIN_DEFROST 2
#define SD_CS 10
#define EEPROM_ADDR_CICLI 0

RTC_DS1307 rtc;           // Oggetto RTC
uint32_t cicloDefrost = 0; // Contatore cicli
bool resetPending = false;  // Flag reset

// ================= FUNZIONI DI SUPPORTO =================

// Invia messaggio su entrambe le seriali
void reply(const String &msg) {
  Serial.println(msg);
  Serial1.println(msg);
}

// Reset software compatibile R4 Minima (ARM Cortex-M0+)
void resetArduino() {
#if defined(ARDUINO_ARCH_RENESAS)
  NVIC_SystemReset(); // corretto per R4 Minima
#else
  void (*resetFunc)(void) = 0;
  resetFunc();
#endif
}

// ================= SETUP =================

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  pinMode(PIN_DEFROST, INPUT_PULLUP);

  // Inizializza RTC
  if (!rtc.begin()) {
    Serial.println("Errore: RTC non trovato");
    while (1);
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Inizializza SD
  if (!SD.begin(SD_CS)) {
    Serial.println("Errore inizializzazione SD");
    while (1);
  }

  // Leggi contatore cicli da EEPROM
  EEPROM.get(EEPROM_ADDR_CICLI, cicloDefrost);
  if (cicloDefrost == 0xFFFFFFFF) cicloDefrost = 0;

  reply("Sistema avviato. Cicli defrost: " + String(cicloDefrost));
}

// ================= LOOP =================

void loop() {
  gestisciComandi();    // Gestione comandi seriali
  gestisciDefrost();    // Controllo defrost
}

// ================= GESTIONE DEFROST =================

void gestisciDefrost() {
  if (digitalRead(PIN_DEFROST) == LOW) {
    // Attesa antirimbalzo
    delay(2000);

    // Inizio ciclo defrost
    DateTime inizio = rtc.now();
    cicloDefrost++;

    File f = SD.open("log.txt", FILE_WRITE);
    if (f) {
      f.print("Ciclo #");
      f.println(cicloDefrost);
      f.print("Inizio: ");
      f.println(inizio.timestamp(DateTime::TIMESTAMP_FULL));
      f.close();
    }

    reply("Defrost #" + String(cicloDefrost) + " iniziato");

    // Attesa fine defrost (pulsante rilasciato)
    while (digitalRead(PIN_DEFROST) == LOW) delay(100);

    DateTime fine = rtc.now();
    uint32_t durata = fine.unixtime() - inizio.unixtime();
    uint32_t min = durata / 60;
    uint32_t sec = durata % 60;

    f = SD.open("log.txt", FILE_WRITE);
    if (f) {
      f.print("Fine: ");
      f.println(fine.timestamp(DateTime::TIMESTAMP_FULL));
      f.print("Durata: ");
      f.print(min);
      f.print(" min ");
      f.print(sec);
      f.println(" sec");
      f.println("----------------------");
      f.close();
    }

    EEPROM.put(EEPROM_ADDR_CICLI, cicloDefrost);
    reply("Defrost terminato. Durata: " + String(min) + " min " + String(sec) + " sec");
  }
}

// ================= GESTIONE COMANDI SERIALI =================

void gestisciComandi() {
  String cmd = "";

  if (Serial.available()) cmd = Serial.readStringUntil('\n');
  else if (Serial1.available()) cmd = Serial1.readStringUntil('\n');
  else return;

  cmd.trim();
  cmd.toUpperCase();

  if (resetPending) {
    if (cmd == "Y") {
      reply("Reset in corso...");
      delay(100);
      resetArduino();
    } else {
      reply("Reset annullato");
      resetPending = false;
    }
    return;
  }

  // ===== COMANDI =====
  if (cmd == "H") {
    reply("=== COMANDI ===");
    reply("H: help");
    reply("L: leggi log SD");
    reply("D: cancella log SD");
    reply("S: mostra contatore");
    reply("R: dump EEPROM");
    reply("C: azzera contatore");
    reply("T: mostra ora corrente");
    reply("X: reset Arduino");
  }
  else if (cmd == "R") {
    reply("EEPROM:");
    for (int i = 0; i < 4; i++) {
      reply(String(EEPROM.read(i), HEX));
    }
  }
  else if (cmd == "C") {
    cicloDefrost = 0;
    EEPROM.put(EEPROM_ADDR_CICLI, cicloDefrost);
    reply("Contatore azzerato");
  }
  else if (cmd == "X") {
    reply("Invia Y per confermare reset");
    resetPending = true;
  }
  else if (cmd == "S") {
    reply("Contatore cicli: " + String(cicloDefrost));
  }
  else if (cmd == "L") {
    File f = SD.open("log.txt");
    if (f) {
      while (f.available()) {
        char c = f.read();
        Serial.write(c);
        Serial1.write(c);
      }
      f.close();
    } else {
      reply("File log.txt non presente");
    }
  }
  else if (cmd == "D") {
    if (SD.exists("log.txt")) {
      SD.remove("log.txt");
      reply("log.txt cancellato");
    } else {
      reply("Nessun log presente");
    }
  }
  else if (cmd == "T") {
    DateTime now = rtc.now();
    reply("Ora corrente: " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) +
          " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()));
  }
  else {
    reply("Comando non valido");
  }
}
