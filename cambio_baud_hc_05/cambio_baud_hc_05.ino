void setup() {
  Serial.begin(9600);        // Monitor IDE
  Serial1.begin(38400);     // AT mode HC-05
  Serial.println("AT MODE READY");
}

void loop() {
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }
  if (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}

