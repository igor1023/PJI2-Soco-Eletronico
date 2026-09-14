#include <SPI.h>
#include <SD.h>

#define CS_PIN   4
#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

void listarArquivos(File diretorio, int nivel) {

  while (true) {

    File arquivo = diretorio.openNextFile();

    if (!arquivo) {
      break;
    }

    // Indentação para mostrar a estrutura de pastas
    for (int i = 0; i < nivel; i++) {
      Serial.print("  ");
    }

    Serial.print(arquivo.name());

    if (arquivo.isDirectory()) {

      Serial.println("/");

      // Entra na pasta e lista seu conteúdo
      listarArquivos(arquivo, nivel + 1);

    } else {

      Serial.print("  -  ");
      Serial.print(arquivo.size());
      Serial.println(" bytes");
    }

    arquivo.close();
  }
}

void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println("Inicializando SPI...");

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  if (!SD.begin(CS_PIN, SPI)) {
    Serial.println("ERRO: Não foi possível inicializar o cartão SD.");
    return;
  }

  Serial.println("Cartão SD inicializado!");
  Serial.println();
  Serial.println("Arquivos encontrados:");
  Serial.println("----------------------");

  File raiz = SD.open("/");

  if (!raiz) {
    Serial.println("ERRO: Não foi possível abrir o diretório raiz.");
    return;
  }

  listarArquivos(raiz, 0);

  raiz.close();

  Serial.println("----------------------");
  Serial.println("Listagem concluída.");
}

void loop() {
}