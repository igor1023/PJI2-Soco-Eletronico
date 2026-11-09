// ============================================================
// SOCO ELETRONICO - ESP32 + FSR + Matriz MAX7219
// Partidas individuais: um jogador por partida, um soco por partida
//
// Fluxo:
//   ESPERANDO JOGADOR  --(segurar botao 3 s)-->  AJUSTE DE DIFICULDADE
//   AJUSTE: clique curto = troca (Facil/Medio/Dificil)
//           segurar 3 s  = confirma e inicia a partida
//   PREPARANDO (alvo) -> VAI! -> soco -> resultado -> multiplicador
//   -> volta para ESPERANDO JOGADOR (nunca inicia sozinho)
//
// Botao GPIO 26 (para GND, pull-up interno)
// Maquina de estados nao-bloqueante
// ============================================================


#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <stdarg.h>
#include <esp_system.h>


// ===================== CONFIGURACOES ========================


// ---- Matriz ----
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4
#define CS_PIN        5
#define BRILHO        5
#define SCROLL_DELAY  35
#define CHAR_SPACING  1
#define BUF_SIZE      64


// ---- Sensor FSR ----
#define SENSOR_PIN            34
#define ADC_MAX               4095
#define TEMPO_CAPTURA_IMPACTO 200


// ---- Jogo ----
#define TEMPO_PREPARACAO  6500
#define TEMPO_LIMITE_SOCO 10000
#define TEMPO_RESULTADO   8000
// Tempo exibindo o resultado final (multiplicador) antes de voltar
// para "Esperando jogador".
#define TEMPO_REINICIO    10000

// Tempo exibindo cada parte do resultado
#define TEMPO_EXIBIR_PONTUACAO 5000

// Tempo para a mensagem "Ajuste de Dificuldade" rolar inteira
// antes de mostrar a dificuldade atual.
#define TEMPO_INTRO_AJUSTE 6000


// ---- Botao ----
#define BOTAO_PIN     26
#define DEBOUNCE_MS   40
#define TEMPO_PRESSAO 3000


// ========================= TIPOS ============================


enum class Estado : uint8_t {
  AGUARDANDO_JOGADOR,
  AJUSTE_DIFICULDADE,
  PREPARANDO,
  MEDINDO,
  MOSTRANDO_RESULTADO,
  FINALIZADO
};


enum class Dificuldade : uint8_t {
  FACIL,
  MEDIO,
  DIFICIL
};


enum class EventoBotao : uint8_t {
  NENHUM,
  CLIQUE_CURTO,
  PRESSAO_LONGA
};


struct ConfigDificuldade {
  uint16_t limiar;
  uint8_t confirmacoes;
  uint16_t larguraFaixa;
};


struct Jogo {
  Estado estado = Estado::AGUARDANDO_JOGADOR;
  uint16_t dano = 0;
  uint16_t alvoA = 0;
  uint16_t alvoB = 0;
  uint8_t multiplicador = 1;
  bool multiplicadorExibido = false;
  uint32_t tEstado = 0;
} jogo;


struct Impacto {
  bool ativo = false;
  uint16_t pico = 0;
  uint8_t confirmacoes = 0;
  uint32_t tInicio = 0;
} impacto;


// ======================== VARIAVEIS =========================


// Matriz
MD_MAX72XX mx(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);


uint8_t curMessage[BUF_SIZE] = "  ";
uint8_t newMessage[BUF_SIZE] = {0};
bool newMessageAvailable = false;


// Dificuldade
Dificuldade dificuldadeAtual = Dificuldade::FACIL;
Dificuldade dificuldadeSelecionada = Dificuldade::FACIL;


ConfigDificuldade configDificuldade = {
  500,  // limiar inicial
  2,    // confirmacoes iniciais
  400   // largura inicial da faixa de pontuacao
};


// Ajuste: true depois que a mensagem introdutoria terminou
// (ou foi pulada por um clique).
bool introAjusteConcluida = false;


// ======================== DISPLAY ===========================


void exibir(const char *fmt, ...) {
  memset(newMessage, 0, BUF_SIZE);


  va_list args;
  va_start(args, fmt);
  vsnprintf((char *)newMessage, BUF_SIZE - 4, fmt, args);
  va_end(args);


  strcat((char *)newMessage, "  ");
  newMessageAvailable = true;
}


void scrollDataSink(uint8_t dev,
                    MD_MAX72XX::transformType_t t,
                    uint8_t col) {
}


uint8_t scrollDataSource(uint8_t dev,
                         MD_MAX72XX::transformType_t t) {
  static uint8_t *p = curMessage;


  static enum {
    NEW_MESSAGE,
    LOAD_CHAR,
    SHOW_CHAR,
    BETWEEN_CHAR
  } state = LOAD_CHAR;


  static uint8_t curLen, showLen;
  static uint8_t cBuf[15];


  uint8_t colData = 0;


  if (newMessageAvailable) {
    state = NEW_MESSAGE;
    mx.clear();
  }


  switch (state) {
    case NEW_MESSAGE:
      memcpy(curMessage, newMessage, BUF_SIZE);
      newMessageAvailable = false;
      p = curMessage;
      state = LOAD_CHAR;
      break;


    case LOAD_CHAR:
      showLen = mx.getChar(*p++, sizeof(cBuf), cBuf);
      curLen = 0;
      state = SHOW_CHAR;


      if (*p == '\0') p = curMessage;
      // fall through


    case SHOW_CHAR:
      colData = cBuf[curLen++];


      if (curLen == showLen) {
        showLen = CHAR_SPACING;
        curLen = 0;
        state = BETWEEN_CHAR;
      }
      break;


    case BETWEEN_CHAR:
      colData = 0;


      if (++curLen == showLen) {
        state = LOAD_CHAR;
      }
      break;


    default:
      state = LOAD_CHAR;
  }


  return colData;
}


void atualizarScroll() {
  static uint32_t prev = 0;
  uint32_t agora = millis();


  if (agora - prev >= SCROLL_DELAY) {
    mx.transform(MD_MAX72XX::TSL);
    prev = agora;
  }
}


// ======================== DIFICULDADE =======================


const char* nomeDificuldade(Dificuldade dificuldade) {
  switch (dificuldade) {
    case Dificuldade::FACIL:   return "Facil";
    case Dificuldade::MEDIO:   return "Medio";
    case Dificuldade::DIFICIL: return "Dificil";
  }


  return "Facil";
}


void aplicarDificuldade(Dificuldade dificuldade) {
  dificuldadeAtual = dificuldade;


  switch (dificuldadeAtual) {
    case Dificuldade::FACIL:
      configDificuldade.limiar = 500;
      configDificuldade.confirmacoes = 2;
      configDificuldade.larguraFaixa = 400;
      break;


    case Dificuldade::MEDIO:
      configDificuldade.limiar = 750;
      configDificuldade.confirmacoes = 3;
      configDificuldade.larguraFaixa = 300;
      break;


    case Dificuldade::DIFICIL:
      configDificuldade.limiar = 1000;
      configDificuldade.confirmacoes = 4;
      configDificuldade.larguraFaixa = 200;
      break;
  }


  Serial.println("\n===== DIFICULDADE CONFIRMADA =====");
  Serial.printf("Nivel: %s\n", nomeDificuldade(dificuldadeAtual));
  Serial.printf("Limiar ADC: %u\n", configDificuldade.limiar);
  Serial.printf("Confirmacoes: %u\n", configDificuldade.confirmacoes);
  Serial.printf("Largura da faixa x3: %u pontos\n",
                configDificuldade.larguraFaixa);
}


void mostrarDificuldade() {
  exibir("%s", nomeDificuldade(dificuldadeSelecionada));


  Serial.printf("Dificuldade selecionada: %s\n",
                nomeDificuldade(dificuldadeSelecionada));
}


void avancarDificuldade() {
  switch (dificuldadeSelecionada) {
    case Dificuldade::FACIL:
      dificuldadeSelecionada = Dificuldade::MEDIO;
      break;


    case Dificuldade::MEDIO:
      dificuldadeSelecionada = Dificuldade::DIFICIL;
      break;


    case Dificuldade::DIFICIL:
      dificuldadeSelecionada = Dificuldade::FACIL;
      break;
  }


  mostrarDificuldade();
}


// ======================== BOTAO =============================


// O botao so tem funcao fora da partida: esperando jogador e ajuste.
// Pressoes iniciadas durante a partida sao ignoradas por completo,
// inclusive se continuarem seguradas depois que a partida terminar.
bool botaoAtivoNoEstado() {
  return jogo.estado == Estado::AGUARDANDO_JOGADOR ||
         jogo.estado == Estado::AJUSTE_DIFICULDADE;
}


EventoBotao atualizarBotao() {
  static bool ultimaLeitura = HIGH;
  static bool estadoEstavel = HIGH;
  static uint32_t tMudanca = 0;

  static bool botaoPressionado = false;
  static uint32_t inicioPressao = 0;
  static bool pressaoLongaReconhecida = false;
  static bool pressaoAceita = false;   // pressao iniciada em estado valido

  uint32_t agora = millis();
  bool leitura = digitalRead(BOTAO_PIN);

  if (leitura != ultimaLeitura) {
    ultimaLeitura = leitura;
    tMudanca = agora;
  }

  // Debounce
  if (leitura != estadoEstavel && agora - tMudanca >= DEBOUNCE_MS) {
    estadoEstavel = leitura;

    if (estadoEstavel == LOW) {
      // Botao acabou de ser pressionado.
      botaoPressionado = true;
      pressaoLongaReconhecida = false;
      inicioPressao = tMudanca;               // inicio real da pressao
      pressaoAceita = botaoAtivoNoEstado();
    }

    else {
      // Botao foi solto.
      botaoPressionado = false;

      // Se a pressao longa ja foi reconhecida (ou a pressao foi ignorada),
      // soltar o botao nao gera nenhum evento.
      if (pressaoAceita && !pressaoLongaReconhecida) {
        return EventoBotao::CLIQUE_CURTO;
      }

      return EventoBotao::NENHUM;
    }
  }

  // Reconhece a pressao longa assim que completar 3 s,
  // sem precisar soltar o botao.
  if (botaoPressionado &&
      pressaoAceita &&
      !pressaoLongaReconhecida &&
      agora - inicioPressao >= TEMPO_PRESSAO) {

    pressaoLongaReconhecida = true;
    return EventoBotao::PRESSAO_LONGA;
  }

  return EventoBotao::NENHUM;
}


// ======================== SENSOR ============================


void resetarImpacto() {
  impacto = Impacto();
}


uint16_t converterEscala(uint16_t valorBruto) {
  return map(valorBruto, 0, ADC_MAX, 0, 1000);
}


bool processarSensor() {
  uint16_t leitura = analogRead(SENSOR_PIN);


  // Aguardando deteccao do impacto
  if (!impacto.ativo) {
    if (leitura >= configDificuldade.limiar) {
      impacto.confirmacoes++;


      if (impacto.confirmacoes >= configDificuldade.confirmacoes) {
        impacto.ativo = true;
        impacto.pico = leitura;
        impacto.tInicio = millis();
        impacto.confirmacoes = 0;
      }
    } else {
      impacto.confirmacoes = 0;
    }


    return false;
  }


  // Captura o maior valor durante 200 ms
  if (leitura > impacto.pico) {
    impacto.pico = leitura;
  }


  if (millis() - impacto.tInicio >= TEMPO_CAPTURA_IMPACTO) {
    return true;
  }


  return false;
}


// ===================== ALVO E MULTIPLICADOR ==================


// Sorteia A e B respeitando o limiar da dificuldade.
// O limiar esta em ADC bruto (0 a 4095), enquanto a pontuacao
// e apresentada na escala convertida (0 a 1000).
void sortearAlvo() {
  const uint16_t largura = configDificuldade.larguraFaixa;
  const uint16_t LIMITE_SUPERIOR_ALVO = 850; // 15% abaixo de 1000


  // Converte o limiar ADC para a mesma escala da pontuacao.
  const uint16_t limiarNaEscala = converterEscala(configDificuldade.limiar);


  // A deve ser pelo menos 15% maior que o limiar convertido.
  // Soma 99 antes da divisao para arredondar para cima.
  const uint16_t minimoA = (limiarNaEscala * 115 + 99) / 100;


  // B = A + largura. Portanto A nao pode ultrapassar este valor.
  const uint16_t maximoA = LIMITE_SUPERIOR_ALVO - largura;


  // Protecao para evitar intervalo impossivel se os parametros mudarem.
  if (minimoA > maximoA) {
    Serial.println("ERRO: configuracao de faixa incompativel com o limiar.");
    jogo.alvoA = minimoA;
    jogo.alvoB = LIMITE_SUPERIOR_ALVO;
  } else {
    // random() inclui o minimo e exclui o limite superior.
    jogo.alvoA = random(minimoA, maximoA + 1);
    jogo.alvoB = jogo.alvoA + largura;
  }


  Serial.println("\n===== ALVO DA RODADA =====");
  Serial.printf("Dificuldade: %s\n", nomeDificuldade(dificuldadeAtual));
  Serial.printf("Limiar ADC bruto: %u / 4095\n", configDificuldade.limiar);
  Serial.printf("Limiar convertido: %u / 1000\n", limiarNaEscala);
  Serial.printf("A minimo (limiar + 15%%): %u\n", minimoA);
  Serial.printf("B maximo (1000 - 15%%): %u\n", LIMITE_SUPERIOR_ALVO);
  Serial.printf("Intervalo sorteado: %u ate %u\n", jogo.alvoA, jogo.alvoB);
}


uint8_t calcularMultiplicador(uint16_t pontuacao) {
  // Faixa principal: multiplicador 3.
  if (pontuacao >= jogo.alvoA && pontuacao <= jogo.alvoB) {
    return 3;
  }


  // Tolerancia de 10% da largura da faixa, para cada lado.
  uint16_t tolerancia = configDificuldade.larguraFaixa / 10;


  if (pontuacao < jogo.alvoA &&
      (jogo.alvoA - pontuacao) <= tolerancia) {
    return 2;
  }


  if (pontuacao > jogo.alvoB &&
      (pontuacao - jogo.alvoB) <= tolerancia) {
    return 2;
  }


  return 1;
}


// ================= TRANSICOES DE ESTADO =====================


void entrarEstado(Estado novo) {
  jogo.estado = novo;
  jogo.tEstado = millis();


  switch (novo) {
    case Estado::AGUARDANDO_JOGADOR:
      resetarImpacto();
      Serial.println("\n===== ESPERANDO JOGADOR =====");
      Serial.println("Segure o botao por 3 s para iniciar.");
      exibir("Esperando jogador");
      break;


    case Estado::AJUSTE_DIFICULDADE:
      // Parte da dificuldade atualmente configurada.
      dificuldadeSelecionada = dificuldadeAtual;
      introAjusteConcluida = false;

      Serial.println("\n===== MODO DE AJUSTE =====");
      exibir("Ajuste de Dificuldade");
      break;


    case Estado::PREPARANDO:
      Serial.println("\n===== PREPARANDO PARA O SOCO =====");
      Serial.printf("Alvo desta rodada: %u a %u\n",
                    jogo.alvoA, jogo.alvoB);

      exibir("Alvo: %u a %u  Alvo: %u a %u  Prepare-se!!",
             jogo.alvoA, jogo.alvoB, jogo.alvoA, jogo.alvoB);
      break;


    case Estado::MEDINDO:
      resetarImpacto();
      Serial.println(">>> SOCO LIBERADO!");
      exibir("VAI!");
      break;


    case Estado::MOSTRANDO_RESULTADO:
      Serial.printf("Pontuacao: %u\n", jogo.dano);
      Serial.printf("Multiplicador: %u\n", jogo.multiplicador);
      exibir("Pontuacao: %u", jogo.dano);
      jogo.multiplicadorExibido = false;
      break;


    case Estado::FINALIZADO:
      Serial.println("\n========= FIM DA PARTIDA =========");
      Serial.printf("PONTUACAO: %u\n", jogo.dano);
      Serial.printf("MULTIPLICADOR: %u\n", jogo.multiplicador);
      exibir("Multiplicador: %u", jogo.multiplicador);
      break;
  }
}


// ======================== JOGO ==============================


// Inicia uma partida nova e independente: zera os dados da anterior,
// sorteia o alvo (com a dificuldade ja confirmada) e comeca a preparacao.
void iniciarPartida() {
  jogo.dano = 0;
  jogo.multiplicador = 1;
  jogo.multiplicadorExibido = false;


  resetarImpacto();
  sortearAlvo();


  entrarEstado(Estado::PREPARANDO);
}


void confirmarDificuldade() {
  aplicarDificuldade(dificuldadeSelecionada);

  Serial.println("\n===== INICIANDO PARTIDA =====");

  // O cronometro da partida (jogo.tEstado) comeca aqui, dentro de
  // entrarEstado(). O botao pode continuar pressionado: ele e ignorado
  // durante a partida e soltar nao gera evento nem altera nenhum tempo.
  iniciarPartida();
}


void registrarSoco(uint16_t dano) {
  jogo.dano = dano;
  jogo.multiplicador = calcularMultiplicador(dano);


  entrarEstado(Estado::MOSTRANDO_RESULTADO);
}


// ===================== MAQUINA DE ESTADOS ===================


void atualizarJogo(EventoBotao evento) {
  uint32_t decorrido = millis() - jogo.tEstado;


  switch (jogo.estado) {

    case Estado::AGUARDANDO_JOGADOR:
      // Parado ate alguem segurar o botao por 3 s.
      // Nunca inicia partida sozinho.
      if (evento == EventoBotao::PRESSAO_LONGA) {
        entrarEstado(Estado::AJUSTE_DIFICULDADE);
      }
      break;


    case Estado::AJUSTE_DIFICULDADE:
      // Pressao longa: confirma e inicia a partida imediatamente.
      if (evento == EventoBotao::PRESSAO_LONGA) {
        confirmarDificuldade();
        return;
      }

      // Clique curto: avanca a dificuldade (pula a introducao se ainda rolando).
      if (evento == EventoBotao::CLIQUE_CURTO) {
        introAjusteConcluida = true;
        avancarDificuldade();
      }

      // Terminada a mensagem introdutoria, mostra a dificuldade atual.
      if (!introAjusteConcluida && decorrido >= TEMPO_INTRO_AJUSTE) {
        introAjusteConcluida = true;
        mostrarDificuldade();
      }
      break;


    case Estado::PREPARANDO:
      if (decorrido >= TEMPO_PREPARACAO &&
          analogRead(SENSOR_PIN) < configDificuldade.limiar) {
        entrarEstado(Estado::MEDINDO);
      }
      break;


    case Estado::MEDINDO:
      if (processarSensor()) {
        registrarSoco(converterEscala(impacto.pico));
      } else if (!impacto.ativo &&
                 decorrido >= TEMPO_LIMITE_SOCO) {
        Serial.println("Tempo esgotado: nenhum soco detectado");
        registrarSoco(0);
      }
      break;


    case Estado::MOSTRANDO_RESULTADO:
      // Primeiro mostra a pontuacao; depois, o multiplicador.
      if (!jogo.multiplicadorExibido &&
          decorrido >= TEMPO_EXIBIR_PONTUACAO) {
        exibir("Multiplicador: %u", jogo.multiplicador);
        jogo.multiplicadorExibido = true;
      }


      if (decorrido >= TEMPO_RESULTADO) {
        entrarEstado(Estado::FINALIZADO);
      }
      break;


    case Estado::FINALIZADO:
      // Fim da partida: volta a esperar o proximo jogador.
      if (decorrido >= TEMPO_REINICIO) {
        entrarEstado(Estado::AGUARDANDO_JOGADOR);
      }
      break;
  }
}


// ======================= SETUP / LOOP =======================


void setup() {
  Serial.begin(115200);
  Serial.println("\n=== MEDIDOR DE IMPACTO FSR ===");


  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);


  pinMode(BOTAO_PIN, INPUT_PULLUP);


  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, BRILHO);
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);


  // Inicializa o gerador aleatorio usando o gerador do ESP32.
  randomSeed(esp_random());


  aplicarDificuldade(Dificuldade::FACIL);
  entrarEstado(Estado::AGUARDANDO_JOGADOR);
}


void loop() {
  EventoBotao evento = atualizarBotao();

  atualizarJogo(evento);
  atualizarScroll();
}
