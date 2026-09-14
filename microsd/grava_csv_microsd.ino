#include <SPI.h>
#include <SD.h>

// ================= CONFIGURAÇÃO DO CARTÃO SD =================

#define SD_CS 4

// ================= CONFIGURAÇÃO DO FSR =================

#define SENSOR 34
#define LIMIAR_IMPACTO 500
#define TEMPO_FIM_IMPACTO 100

bool emImpacto = false;
int maiorImpacto = 0;
uint32_t ultimoImpacto = 0;

// ================= CONFIGURAÇÃO DO JOGO =================

#define TOTAL_TENTATIVAS 3

#define TEMPO_PREPARACAO 2000
#define TEMPO_RESULTADO 10000

enum EstadoJogo {
  PREPARANDO,
  MEDINDO,
  MOSTRANDO_RESULTADO,
  FINALIZADO
};

EstadoJogo estadoJogo = PREPARANDO;

int tentativaAtual = 0;

int pontos[TOTAL_TENTATIVAS] = {
  0,
  0,
  0
};

uint32_t tempoEstado = 0;


// ============================================================
// INICIALIZAÇÃO DO CARTÃO SD
// ============================================================

void iniciarCartaoSD() {

  Serial.println("Inicializando cartão SD...");

  if (!SD.begin(SD_CS)) {

    Serial.println("ERRO: Não foi possível inicializar o cartão SD.");

    return;
  }

  Serial.println("Cartão SD inicializado com sucesso!");

  // Se o arquivo ainda não existir, cria e coloca o cabeçalho
  if (!SD.exists("/impactos.csv")) {

    File arquivo = SD.open("/impactos.csv", FILE_WRITE);

    if (!arquivo) {
      Serial.println("ERRO: Não foi possível criar impactos.csv");
      return;
    }

    arquivo.println("Tentativa;Pontuacao");

    arquivo.close();

    Serial.println("Arquivo impactos.csv criado.");
  }
}


// ============================================================
// GRAVAR RESULTADO NO CSV
// ============================================================

void gravarImpactoCSV(int tentativa, int valor) {

  File arquivo = SD.open("/impactos.csv", FILE_APPEND);

  if (!arquivo) {

    Serial.println("ERRO: Não foi possível abrir impactos.csv");

    return;
  }

  arquivo.print(tentativa);
  arquivo.print(";");
  arquivo.println(valor);

  arquivo.close();

  Serial.print("Gravado no CSV: ");
  Serial.print(tentativa);
  Serial.print(";");
  Serial.println(valor);
}


// ============================================================
// PREPARAR NOVA TENTATIVA
// ============================================================

void iniciarTentativa() {

  tentativaAtual++;

  Serial.println();
  Serial.print("========== TENTATIVA ");
  Serial.print(tentativaAtual);
  Serial.println(" ==========");

  Serial.println("Preparando...");

  tempoEstado = millis();

  estadoJogo = PREPARANDO;
}


// ============================================================
// FINALIZAR IMPACTO
// ============================================================

void finalizarImpacto() {

  emImpacto = false;

  // Guarda a pontuação da tentativa
  pontos[tentativaAtual - 1] = maiorImpacto;

  Serial.print("Tentativa ");
  Serial.print(tentativaAtual);

  Serial.print(" - Maior impacto: ");
  Serial.println(maiorImpacto);

  // Grava no cartão SD
  gravarImpactoCSV(
    tentativaAtual,
    maiorImpacto
  );

  // Vai para o estado de resultado
  tempoEstado = millis();

  estadoJogo = MOSTRANDO_RESULTADO;
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("=================================");
  Serial.println("       MEDIDOR DE IMPACTO FSR");
  Serial.println("=================================");

  // Inicializa cartão SD
  iniciarCartaoSD();

  // Começa a primeira tentativa
  iniciarTentativa();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  uint32_t agora = millis();


  // ==========================================================
  // ESTADO: PREPARANDO
  // ==========================================================

  if (estadoJogo == PREPARANDO) {

    // Aguarda os 2 segundos de preparação

    if (agora - tempoEstado >= TEMPO_PREPARACAO) {

      Serial.println(">>> SOCO LIBERADO!");
      Serial.println("Aguardando impacto...");

      maiorImpacto = 0;

      emImpacto = false;

      estadoJogo = MEDINDO;
    }
  }


  // ==========================================================
  // ESTADO: MEDINDO
  // ==========================================================

  else if (estadoJogo == MEDINDO) {

    // Lê o FSR
    int valor = analogRead(SENSOR);


    // --------------------------------------------------------
    // DETECTOU IMPACTO
    // --------------------------------------------------------

    if (valor > LIMIAR_IMPACTO) {

      // Primeiro valor acima do limiar
      if (!emImpacto) {

        emImpacto = true;

        maiorImpacto = valor;

        Serial.println(">>> IMPACTO DETECTADO!");
      }


      // Guarda o maior valor
      if (valor > maiorImpacto) {

        maiorImpacto = valor;
      }


      // Atualiza o momento da última leitura
      // acima do limiar

      ultimoImpacto = agora;
    }


    // --------------------------------------------------------
    // VERIFICA SE O IMPACTO TERMINOU
    // --------------------------------------------------------

    if (
      emImpacto &&
      (agora - ultimoImpacto >= TEMPO_FIM_IMPACTO)
    ) {

      finalizarImpacto();
    }
  }


  // ==========================================================
  // ESTADO: MOSTRANDO RESULTADO
  // ==========================================================

  else if (estadoJogo == MOSTRANDO_RESULTADO) {

    // Aguarda 10 segundos antes da próxima tentativa

    if (agora - tempoEstado >= TEMPO_RESULTADO) {

      // Ainda existem tentativas?
      if (tentativaAtual < TOTAL_TENTATIVAS) {

        iniciarTentativa();
      }

      // Terminou as três tentativas
      else {

        Serial.println();
        Serial.println("=================================");
        Serial.println("          FIM DO JOGO");
        Serial.println("=================================");

        Serial.print("Tentativa 1: ");
        Serial.println(pontos[0]);

        Serial.print("Tentativa 2: ");
        Serial.println(pontos[1]);

        Serial.print("Tentativa 3: ");
        Serial.println(pontos[2]);

        Serial.println();
        Serial.println("Resultados gravados em impactos.csv");

        estadoJogo = FINALIZADO;
      }
    }
  }


  // ==========================================================
  // ESTADO: FINALIZADO
  // ==========================================================

  else if (estadoJogo == FINALIZADO) {

    // Não faz mais nada.
  }
}