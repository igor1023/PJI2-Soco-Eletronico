// Código teste com cartao SD

#include<Wire.h>
#include<SPI.h>
#include<SD.h>

int cs = 4;

void setup() {
  
  Serial.begin(9600);
  if(!SD.begin(cs)){
    Serial.println("Cartao SD card não pode inicializar...");
    return;
  } else{
    Serial.println("Cartão SD inicializado...");
  }

}

void loop() {

  delay(2000);
  Serial.println("Teste com cartao SD...");

}
