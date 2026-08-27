// Código para medir a tensão lida no pino do potenciometro
// circuito divisor de tensao

#define FSR_PIN 34

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetPinAttenuation(FSR_PIN, ADC_11db);
}

void loop() {
  int adc = analogRead(FSR_PIN);
  int mv = analogReadMilliVolts(FSR_PIN);

  Serial.print("ADC = ");
  Serial.print(adc);

  Serial.print(" | mV = ");
  Serial.print(mv);

  Serial.print(" | V = ");
  Serial.println(mv / 1000.0, 3);

  delay(500);
}