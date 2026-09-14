#include <SPI.h>
#include <SD.h>


#define CS_PIN   4
#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  if (!SD.begin(CS_PIN, SPI)) {
    Serial.println("ERRO: Não foi possível inicializar o cartão SD.");
    return;
  }

  Serial.println("Cartão SD inicializado!");

  // Abre o arquivo para escrita
  File arquivo = SD.open("/teste.txt", FILE_WRITE);

  if (!arquivo) {
    Serial.println("ERRO: Não foi possível abrir o arquivo.");
    return;
  }

  arquivo.println("Olá, cartão SD!");
  arquivo.println("Este é um teste da ESP32.");
  arquivo.println("Linha 3");

  arquivo.close();

  Serial.println("Dados escritos com sucesso!");

  // Agora vamos ler o arquivo
  arquivo = SD.open("/teste.txt");

  if (!arquivo) {
    Serial.println("ERRO: Não foi possível abrir o arquivo para leitura.");
    return;
  }

  Serial.println("Conteúdo do arquivo:");

  while (arquivo.available()) {
    Serial.write(arquivo.read());
  }

  arquivo.close();
}

void loop() {
}