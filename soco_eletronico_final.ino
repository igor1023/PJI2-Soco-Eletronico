// ============================================================
// SOCO ELETRONICO - ESP32 + FSR + Matriz MAX7219
//
// Fluxo:
//   AGUARDANDO JOGADOR (ao ligar e depois de cada soco)
//     -> clique curto -> RANGE DO SOCO (mostra o intervalo e o FSR
//        fica pronto para ler o soco)
//     -> soco detectado -> pontuacao -> multiplicador (+ som)
//     -> volta sozinho para AGUARDANDO JOGADOR
//
//   Segurar o botao 5 s SOMENTE em AGUARDANDO JOGADOR -> AJUSTE DE DIFICULDADE
//     clique curto = troca (FACIL / MEDIO / DIFICIL)
//     segurar 5 s  = confirma e volta para AGUARDANDO JOGADOR
//
// Dificuldade inicial: MEDIO (sempre que o equipamento e ligado).
// Botao GPIO 26 (para GND, pull-up interno)
// Maquina de estados nao-bloqueante
// ============================================================


#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <stdarg.h>
#include <esp_system.h>


// ===================== CONFIGURACOES ========================

// ---- Buzzer ----
#define BUZZER_PIN 25

// ---- Matriz ----
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4
#define CS_PIN        5
#define BRILHO        5
#define SCROLL_DELAY_DEFAULT 30
#define CHAR_SPACING  1
#define BUF_SIZE      64


// ---- Sensor FSR ----
#define SENSOR_PIN            34
#define ADC_MAX               4095
#define TEMPO_CAPTURA_IMPACTO 200


// ---- Jogo ----
#define TEMPO_RESULTADO   8000
// Tempo exibindo o resultado final (multiplicador) antes de voltar
// para "AGUARDANDO JOGADOR".
#define TEMPO_REINICIO    10000

// Tempo exibindo cada parte do resultado
#define TEMPO_EXIBIR_PONTUACAO 5000

// Tempo para a mensagem "AJUSTE DE DIFICULDADE" rolar inteira
// antes de mostrar a dificuldade atual.
#define TEMPO_INTRO_AJUSTE 7000


// ---- Botao ----
#define BOTAO_PIN     26
#define DEBOUNCE_MS   40
#define TEMPO_PRESSAO 3000


// ========================= TIPOS ============================


enum class Estado : uint8_t {
  AGUARDANDO_JOGADOR,   // espera o clique do botao para iniciar
  RANGE,                // pronto: FSR monitorando o soco
  AJUSTE_DIFICULDADE,
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
  bool sensorArmado = false;   // true quando o FSR ja ficou abaixo do limiar
  uint32_t tEstado = 0;
} jogo;


struct Impacto {
  bool ativo = false;
  uint16_t pico = 0;
  uint8_t confirmacoes = 0;
  uint32_t tInicio = 0;
} impacto;


// ======================== VARIAVEIS =========================

int scroll_delay = SCROLL_DELAY_DEFAULT;

// Controle da sequencia sonora (nao bloqueante)
struct NotaBuzzer {
  uint16_t frequencia;   // Hz
  uint16_t duracao;      // ms
  uint16_t intervalo;    // pausa apos a nota, em ms
};

const NotaBuzzer sequencia1x[] = {
  {330, 650, 140},  // nota grave
  {247, 850, 0}     // final descendente
};

const NotaBuzzer sequencia2x[] = {
  {440, 420, 110},  // La
  {554, 420, 110},  // Do#
  {659, 650, 0}     // Mi
};

const NotaBuzzer sequencia3x[] = {
  {523, 350, 90},   // Do
  {659, 350, 90},   // Mi
  {784, 350, 90},   // Sol
  {988, 450, 90},   // Si
  {1047, 900, 0}    // Do agudo prolongado
};

const NotaBuzzer* sequenciaBuzzer = nullptr;
uint8_t quantidadeNotasBuzzer = 0;
uint8_t indiceNotaBuzzer = 0;
bool buzzerAtivo = false;
bool buzzerEmPausa = false;
uint32_t inicioEtapaBuzzer = 0;

// Matriz
MD_MAX72XX mx(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);


uint8_t curMessage[BUF_SIZE] = "  ";
uint8_t newMessage[BUF_SIZE] = {0};
bool newMessageAvailable = false;


// Dificuldade (inicial fixa: MEDIO)
Dificuldade dificuldadeAtual = Dificuldade::MEDIO;
Dificuldade dificuldadeSelecionada = Dificuldade::MEDIO;


ConfigDificuldade configDificuldade = {
  750,  // limiar inicial (MEDIO)
  3,    // confirmacoes iniciais (MEDIO)
  75   // largura inicial da faixa de pontuacao (MEDIO)
};


// Ajuste: true depois que a mensagem introdutoria terminou
// (ou foi pulada por um clique).
bool introAjusteConcluida = false;


// ======================== BUZZER ===========================

// Modulo com transistor interno e acionamento em nivel LOW.
// GPIO HIGH = repouso (desligado).
// tone() gera os pulsos de frequencia para produzir as notas.

#define BUZZER_NIVEL_REPOUSO HIGH

void pararBuzzer() {
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, BUZZER_NIVEL_REPOUSO);

  buzzerAtivo = false;
  buzzerEmPausa = false;
  sequenciaBuzzer = nullptr;
  quantidadeNotasBuzzer = 0;
  indiceNotaBuzzer = 0;
}

void iniciarNotaBuzzer() {
  if (sequenciaBuzzer == nullptr ||
      indiceNotaBuzzer >= quantidadeNotasBuzzer) {
    pararBuzzer();
    return;
  }

  const NotaBuzzer& nota = sequenciaBuzzer[indiceNotaBuzzer];

  tone(BUZZER_PIN, nota.frequencia);

  buzzerEmPausa = false;
  buzzerAtivo = true;
  inicioEtapaBuzzer = millis();
}

void iniciarBuzzer(uint8_t multiplicador) {
  pararBuzzer();

  switch (multiplicador) {
    case 1:
      sequenciaBuzzer = sequencia1x;
      quantidadeNotasBuzzer =
        sizeof(sequencia1x) / sizeof(sequencia1x[0]);
      break;

    case 2:
      sequenciaBuzzer = sequencia2x;
      quantidadeNotasBuzzer =
        sizeof(sequencia2x) / sizeof(sequencia2x[0]);
      break;

    case 3:
      sequenciaBuzzer = sequencia3x;
      quantidadeNotasBuzzer =
        sizeof(sequencia3x) / sizeof(sequencia3x[0]);
      break;

    default:
      return;
  }

  indiceNotaBuzzer = 0;
  iniciarNotaBuzzer();
}

void atualizarBuzzer() {
  if (!buzzerAtivo || sequenciaBuzzer == nullptr) {
    return;
  }

  uint32_t agora = millis();
  const NotaBuzzer& nota = sequenciaBuzzer[indiceNotaBuzzer];

  if (!buzzerEmPausa) {

    if (agora - inicioEtapaBuzzer >= nota.duracao) {

      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, BUZZER_NIVEL_REPOUSO);

      if (nota.intervalo > 0) {
        buzzerEmPausa = true;
        inicioEtapaBuzzer = agora;
      } else {
        indiceNotaBuzzer++;

        if (indiceNotaBuzzer >= quantidadeNotasBuzzer) {
          pararBuzzer();
        } else {
          iniciarNotaBuzzer();
        }
      }
    }

  } else {

    if (agora - inicioEtapaBuzzer >= nota.intervalo) {
      indiceNotaBuzzer++;

      if (indiceNotaBuzzer >= quantidadeNotasBuzzer) {
        pararBuzzer();
      } else {
        iniciarNotaBuzzer();
      }
    }
  }
}



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


  if (agora - prev >= scroll_delay) {
    mx.transform(MD_MAX72XX::TSL);
    prev = agora;
  }
}


// ======================== DIFICULDADE =======================


// Textos em ASCII maiusculo (a fonte da matriz nao tem acentos).
const char* nomeDificuldade(Dificuldade dificuldade) {
  switch (dificuldade) {
    case Dificuldade::FACIL:   return "FACIL";
    case Dificuldade::MEDIO:   return "MEDIO";
    case Dificuldade::DIFICIL: return "DIFICIL";
  }


  return "MEDIO";
}


void aplicarDificuldade(Dificuldade dificuldade) {
  dificuldadeAtual = dificuldade;


  switch (dificuldadeAtual) {
    case Dificuldade::FACIL:
      configDificuldade.limiar = 500;
      configDificuldade.confirmacoes = 2;
      configDificuldade.larguraFaixa = 100;
      break;


    case Dificuldade::MEDIO:
      configDificuldade.limiar = 750;
      configDificuldade.confirmacoes = 3;
      configDificuldade.larguraFaixa = 75;
      break;


    case Dificuldade::DIFICIL:
      configDificuldade.limiar = 1000;
      configDificuldade.confirmacoes = 4;
      configDificuldade.larguraFaixa = 50;
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


// ======================== BOTAO =============================


// A pressao longa (5 s) so e reconhecida em AGUARDANDO JOGADOR (para abrir
// o ajuste) e em AJUSTE DE DIFICULDADE (para confirmar). Nos demais estados
// o botao nao faz nada, entao a captura do soco nunca e interrompida por ele.
bool pressaoLongaPermitida() {
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
    }

    else {
      // Botao foi solto.
      botaoPressionado = false;

      // Se a pressao longa ja foi reconhecida, soltar o botao
      // nao gera nenhum evento.
      if (!pressaoLongaReconhecida) {
        return EventoBotao::CLIQUE_CURTO;
      }

      return EventoBotao::NENHUM;
    }
  }

  // Reconhece a pressao longa assim que completar 5 s,
  // sem precisar soltar o botao.
  if (botaoPressionado &&
      !pressaoLongaReconhecida &&
      agora - inicioPressao >= TEMPO_PRESSAO &&
      pressaoLongaPermitida()) {

    pressaoLongaReconhecida = true;
    return EventoBotao::PRESSAO_LONGA;
  }

  return EventoBotao::NENHUM;
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


  Serial.println("\n===== ALVO DO PROXIMO SOCO =====");
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


  // Tolerancia de 15% da largura da faixa, para cada lado.
  uint16_t tolerancia = configDificuldade.larguraFaixa / 15;


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
      pararBuzzer();
      resetarImpacto();
      jogo.sensorArmado = false;

      Serial.println("\n===== AGUARDANDO JOGADOR =====");
      scroll_delay = 25;
      exibir("AGUARDANDO JOGADOR - CLIQUE NO BOTAO PARA JOGAR -");
      break;


    case Estado::RANGE:
      // Jogador iniciou: novo alvo, captura zerada e sensor desarmado
      // ate o FSR ficar abaixo do limiar (evita contar pressao residual
      // como soco).
      resetarImpacto();
      jogo.sensorArmado = false;
      sortearAlvo();

      Serial.println("\n===== RANGE DO SOCO - PRONTO =====");

      // O alvo (faixa do x3) fica visivel durante todo o estado RANGE,
      // rolando em loop ate o soco.
      scroll_delay = SCROLL_DELAY_DEFAULT;
      exibir("ALVO: %u A %u -> GO! GO! GO! ->", jogo.alvoA, jogo.alvoB);
      break;


    case Estado::AJUSTE_DIFICULDADE:
      pararBuzzer();
      resetarImpacto();

      // Parte da dificuldade atualmente configurada.
      dificuldadeSelecionada = dificuldadeAtual;
      introAjusteConcluida = false;

      Serial.println("\n===== MODO DE AJUSTE =====");

      scroll_delay = SCROLL_DELAY_DEFAULT;
      exibir("AJUSTE DE DIFICULDADE");
      break;


    case Estado::MOSTRANDO_RESULTADO:
      Serial.printf("Pontuacao: %u\n", jogo.dano);
      Serial.printf("Multiplicador: %u\n", jogo.multiplicador);
      exibir("Pontuacao: %u", jogo.dano);
      jogo.multiplicadorExibido = false;
      break;


    case Estado::FINALIZADO:
      Serial.println("\n========= FIM DO SOCO =========");
      Serial.printf("PONTUACAO: %u\n", jogo.dano);
      Serial.printf("MULTIPLICADOR: %u\n", jogo.multiplicador);
      // exibir("Multiplicador: %u", jogo.multiplicador);
      break;
  }
}


// ======================== JOGO ==============================


void confirmarDificuldade() {
  aplicarDificuldade(dificuldadeSelecionada);

  Serial.println("\n===== RETORNANDO AO AGUARDANDO JOGADOR =====");

  // Os temporizadores do jogo (jogo.tEstado) nascem em entrarEstado().
  // O botao pode continuar pressionado: soltar nao gera evento e
  // nenhum tempo e compensado ou alterado por causa dele.
  entrarEstado(Estado::AGUARDANDO_JOGADOR);
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
      // Pressao longa (5 s): abre o ajuste de dificuldade.
      if (evento == EventoBotao::PRESSAO_LONGA) {
        entrarEstado(Estado::AJUSTE_DIFICULDADE);
        return;
      }

      // Clique curto: inicia a rodada (mostra o intervalo e arma o FSR).
      if (evento == EventoBotao::CLIQUE_CURTO) {
        entrarEstado(Estado::RANGE);
      }
      break;


    case Estado::RANGE:
      // Antes de aceitar socos, espera o FSR ficar em repouso.
      if (!jogo.sensorArmado) {
        if (analogRead(SENSOR_PIN) < configDificuldade.limiar) {
          jogo.sensorArmado = true;
        }
        break;
      }

      // Monitoramento: ao detectar o soco, captura o pico
      // (200 ms) e so entao devolve true.
      if (processarSensor()) {
        registrarSoco(converterEscala(impacto.pico));
      }
      break;


    case Estado::AJUSTE_DIFICULDADE:
      // Pressao longa: confirma e volta ao AGUARDANDO JOGADOR.
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


    case Estado::MOSTRANDO_RESULTADO:
      // FSR nao e lido enquanto o resultado e exibido.
      // Primeiro mostra a pontuacao; depois, o multiplicador.
      if (!jogo.multiplicadorExibido &&
          decorrido >= TEMPO_EXIBIR_PONTUACAO) {
        
        iniciarBuzzer(jogo.multiplicador);

        scroll_delay = SCROLL_DELAY_DEFAULT;
        exibir("Multiplicador: %u", jogo.multiplicador);
        jogo.multiplicadorExibido = true;
      }


      if (decorrido >= TEMPO_RESULTADO) {
        entrarEstado(Estado::FINALIZADO);
      }
      break;


    case Estado::FINALIZADO:
      // Fim da apresentacao: volta sozinho para AGUARDANDO JOGADOR.
      if (decorrido >= TEMPO_REINICIO) {
        entrarEstado(Estado::AGUARDANDO_JOGADOR);
      }
      break;
  }
}


// ======================= SETUP / LOOP =======================


void setup() {
  Serial.begin(115200);
  Serial.println("\n=== MEDIDOR DE IMPACTO FSR - SOCO ELETRONICO ===");

  pinMode(BUZZER_PIN, OUTPUT);
digitalWrite(BUZZER_PIN, BUZZER_NIVEL_REPOUSO);

  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);


  pinMode(BOTAO_PIN, INPUT_PULLUP);


  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, BRILHO);
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);


  // Inicializa o gerador aleatorio usando o gerador do ESP32.
  randomSeed(esp_random());


  // Dificuldade inicial fixa: MEDIO. Inicia aguardando o jogador.
  aplicarDificuldade(Dificuldade::MEDIO);
  entrarEstado(Estado::AGUARDANDO_JOGADOR);
}


void loop() {

  atualizarBuzzer();

  EventoBotao evento = atualizarBotao();

  atualizarJogo(evento);
  atualizarScroll();
}
