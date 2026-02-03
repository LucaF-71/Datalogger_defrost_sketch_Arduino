#include "RTClib.h"
#include <SD.h>
#include <EEPROM.h>
#include <SPI.h>

// ================= CONFIGURAZIONE =================

#define PIN_DEFROST 2
#define SD_CS 10
#define EEPROM_ADDR_CICLI 0

#define STX '@'  // Start frame
#define ETX '#'  // End frame

RTC_DS1307 rtc;
uint32_t cicloDefrost = 0;
bool resetPending = false;

// Buffer ricezione
String rxBuffer = "";

// ================= FUNZIONI DI SUPPORTO =================

// Calcola checksum XOR su tutti i caratteri del messaggio (CMD|LEN|PAYLOAD)
byte calcChecksum(const String &s) {
  byte chk = 0;
  for (unsigned int i = 0; i < s.length(); i++) {
    chk ^= s[i];
  }
  return chk;
}

// Invia frame strutturato su entrambe le seriali
void sendFrame(const String &cmd, const String &payload) {
  String len = String(payload.length());
  String body = cmd + "|" + len + "|" + payload;
  byte chk = calcChecksum(body);

  Serial.print(STX);
  Serial.print(body);
  Serial.print("|");
  if (chk < 16) Serial.print("0");
  Serial.print(chk, HEX);
  Serial.print(ETX);
  Serial.println();

  Serial1.print(STX);
  Serial1.print(body);
  Serial1.print("|");
  if (chk < 16) Serial1.print("0");
  Serial1.print(chk, HEX);
  Serial1.print(ETX);
  Serial1.println();
}

// Messaggi di protocollo
void sendACK() {
  sendFrame("ACK", "");
}
void sendNACK(const String &err) {
  sendFrame("NACK", err);
}



// ================= SETUP =================

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  pinMode(PIN_DEFROST, INPUT_PULLUP);

  if (!rtc.begin()) {
    sendFrame("ERR", "RTC non trovato");
    while (1)
      ;
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (!SD.begin(SD_CS)) {
    sendFrame("ERR", "SD init fallita");
    while (1)
      ;
  }

  EEPROM.get(EEPROM_ADDR_CICLI, cicloDefrost);
  if (cicloDefrost == 0xFFFFFFFF) cicloDefrost = 0;

  sendFrame("INFO", "Sistema avviato. Cicli=" + String(cicloDefrost));
}

// ================= LOOP =================

void loop() {
  gestisciSeriale();  // parsing frame
  gestisciDefrost();  // logica defrost
}

// ================= GESTIONE DEFROST =================

void gestisciDefrost() {
  if (digitalRead(PIN_DEFROST) == LOW) {
    delay(2000);  // antirimbalzo

    DateTime inizio = rtc.now();
    cicloDefrost++;

    //scrittura sd inizio ciclo defrost
    File f = SD.open("log.txt", FILE_WRITE);
    if (f) {
      f.print("Ciclo n.ro");
      f.println(cicloDefrost);
      f.print("Inizio: ");
      f.println(inizio.timestamp(DateTime::TIMESTAMP_FULL));
      f.close();
    }

    //invio ad applicazione pc notifica inizio ciclo defrost
    sendFrame("INFO", "Defrost n.ro" + String(cicloDefrost));
    sendFrame("INFO", " iniziato" + inizio.timestamp(DateTime::TIMESTAMP_FULL));

    // Attesa fine defrost (pulsante rilasciato)
    while (digitalRead(PIN_DEFROST) == LOW) delay(100);

    DateTime fine = rtc.now();
    uint32_t durata = fine.unixtime() - inizio.unixtime();
    uint32_t min = durata / 60;
    uint32_t sec = durata % 60;

    //scrittura sd fine ciclo defrost
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

    //invio ad applicazione pc notifica fine ciclo defrost
    sendFrame("INFO",
              "Fine: " + fine.timestamp(DateTime::TIMESTAMP_FULL));

    sendFrame("INFO",
              "Durata: " + String(durata / 60) + "m " + String(durata % 60) + "s");

    //scrittura in memoria Arduino nuovo valore contatore cicli
    EEPROM.put(EEPROM_ADDR_CICLI, cicloDefrost);
  }
}

// ================= PARSING SERIAL FRAME =================

void gestisciSeriale() {
  while (Serial.available()) processChar(Serial.read());
  while (Serial1.available()) processChar(Serial1.read());
}

void processChar(char c) {
  if (c == STX) rxBuffer = "";
  else if (c == ETX) {
    processFrame(rxBuffer);
    rxBuffer = "";
  } else {
    rxBuffer += c;
  }
}

void processFrame(const String &frame) {
  // frame = CMD|LEN|PAYLOAD|CHK
  int p1 = frame.indexOf('|');
  int p2 = frame.indexOf('|', p1 + 1);
  int p3 = frame.lastIndexOf('|');  // ultimo delimitatore per checksum

  if (p1 < 0 || p2 < 0 || p3 < 0 || p3 <= p2) {
    sendNACK("errore_frame");  // frame malformato
    return;
  }

  String cmd = frame.substring(0, p1);
  int len = frame.substring(p1 + 1, p2).toInt();
  String payload = frame.substring(p2 + 1, p3);
  String chkStr = frame.substring(p3 + 1);

  if (payload.length() != len) {
    sendNACK("errore_lunghezza_payload");  // lunghezza errata
    return;
  }

  byte chkCalc = calcChecksum(cmd + "|" + String(len) + "|" + payload);
  byte chkRx = strtol(chkStr.c_str(), NULL, 16);

  if (chkCalc != chkRx) {
    sendNACK("checksum_errrato");  // checksum errato
    return;
  }

  // ================= ACK =================
  sendACK();

  // ================= COMANDI APPLICATIVI =================


  if (cmd == "CHECK_DATALOGGER_DEFROST") {
    sendFrame("INFO", "HI_DATALOGGER_DEFROST");
  }

  if (cmd == "H") {

   sendFrame("HELP", "=== COMANDI ===");
    sendFrame("HELP", "H: help");
    sendFrame("HELP", "L: leggi log SD");
    sendFrame("HELP", "D: cancella log SD");
    sendFrame("HELP", "S: mostra contatore");
    sendFrame("HELP", "R: dump EEPROM");
    sendFrame("HELP", "T: mostra ora corrente");

  } else if (cmd == "R") {
    String dump = "";
    for (int i = 0; i < 4; i++) dump += String(EEPROM.read(i), HEX) + " ";
    sendFrame("EEPROM", dump);

  } else if (cmd == "D") {
    sendFrame("INFO", "Invia Y per confermare cancellazione file LOG");
    resetPending = true;
  } else if (cmd == "Y") {
    if (resetPending) {
      sendFrame("INFO", "Cancellazione in corso...");
      delay(100);
      cancellaLog();
    }
  } else if (cmd == "S") {
    sendFrame("CNT", "N.ro cicli defrost memorizzati: " + String(cicloDefrost));
  } else if (cmd == "L") {
    File f = SD.open("log.txt");
    if (f) {
      String logTxt = "";
      while (f.available()) logTxt += (char)f.read();
      f.close();
      sendFrame("LOG", logTxt);
    } else {
      sendFrame("ERR", "log.txt assente");
    }
  } else if (cmd == "T") {
    DateTime now = rtc.now();
    String t = String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());
    sendFrame("TIME", t);
  } else {
    sendNACK("04");  // comando non valido
  }
}

void cancellaLog() {

  if (SD.exists("log.txt")) {
    SD.remove("log.txt");
    cicloDefrost = 0;
    EEPROM.put(EEPROM_ADDR_CICLI, cicloDefrost);
    sendFrame("INFO", "Contatore azzerato");
    sendFrame("INFO", "log.txt cancellato");
  } else {
    sendFrame("INFO", "Nessun log presente");
  }
}
