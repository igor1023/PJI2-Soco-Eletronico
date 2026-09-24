// ============================================================
// SOCO ELETRÔNICO - ESP32 + FSR + Matriz MAX7219
// 3 socos -> dano de cada um -> maior dano exibido no final
// Máquina de estados 100% não-bloqueante (sem delay)
// ============================================================

#include <MD_MAX72xx.h>
#include <SPI.h>

// ===================== CONFIGURAÇÕES ========================

// ---- Matriz ----
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4
#define CS_PIN        5           // CLK=18 e DIN=23 (SPI de hardware)
#define BRILHO        5           // 0..15
#define SCROLL_DELAY  45          // ms por coluna (menor = mais rápido)
#define CHAR_SPACING  1           // colunas entre caracteres
#define BUF_SIZE      48

// ---- Sensor ----
#define SENSOR_PIN            34
#define ADC_MAX               4095
#define LIMIAR_IMPACTO        500   // valor bruto que caracteriza um soco
#define LEITURAS_CONFIRMACAO  2     // leituras seguidas acima do limiar (filtra ruído)
#define TEMPO_FIM_IMPACTO     100   // ms abaixo do limiar => soco terminou

// ---- Jogo ----
#define TOTAL_TENTATIVAS  3
#define TEMPO_PREPARACAO  7000      // ms com FSR ignorado (tempo p/ "Prepare-se x/3" rolar inteiro)
#define TEMPO_LIMITE_SOCO 15000     // ms esperando o soco (senão valor = 0)
#define TEMPO_RESULTADO   5250      // ms mostrando "Soco i: v v v" (rola inteiro)
#define TEMPO_REINICIO    30000     // ms no FINALIZADO até reiniciar sozinho
#define BOTAO_PIN         27        // botão p/ novo jogo (para GND, pull-up interno)
#define DEBOUNCE_MS       40

// ========================= TIPOS ============================

enum class Estado : uint8_t {
  PREPARANDO,
  MEDINDO,
  MOSTRANDO_RESULTADO,
  FINALIZADO
};

struct Jogo {
  Estado   estado = Estado::PREPARANDO;
  uint8_t  tentativa = 0;                    // 1..TOTAL_TENTATIVAS
  uint16_t dano[TOTAL_TENTATIVAS] = {0};
  uint16_t melhor = 0;
  uint32_t tEstado = 0;                      // instante em que entrou no estado
} jogo;

struct Impacto {
  bool     ativo = false;
  uint16_t pico = 0;
  uint8_t  confirmacoes = 0;
  uint32_t tUltimoAcima = 0;
} impacto;

// ======================== DISPLAY ===========================

MD_MAX72XX mx(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

uint8_t curMessage[BUF_SIZE] = "  ";
uint8_t newMessage[BUF_SIZE];
bool    newMessageAvailable = false;

// Único ponto de entrada para trocar o texto exibido
void exibir(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf((char *)newMessage, BUF_SIZE - 3, fmt, args);
  va_end(args);
  strcat((char *)newMessage, "   ");        // espaço entre repetições
  newMessageAvailable = true;
}

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col) {}

// Callback: fornece a próxima coluna de pixels ao scroll
uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t) {
  static uint8_t *p = curMessage;
  static enum { NEW_MESSAGE, LOAD_CHAR, SHOW_CHAR, BETWEEN_CHAR } state = LOAD_CHAR;
  static uint8_t curLen, showLen;
  static uint8_t cBuf[15];
  uint8_t colData = 0;

  if (newMessageAvailable) {                // troca imediata de mensagem
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
      if (*p == '\0') p = curMessage;       // fim: recomeça a mensagem
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
      if (++curLen == showLen) state = LOAD_CHAR;
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

// ======================== BOTÃO =============================

// Retorna true uma única vez por clique (com debounce)
bool botaoPressionado() {
  static bool estavaPressionado = false;
  static uint32_t tMudanca = 0;
  bool lido = (digitalRead(BOTAO_PIN) == LOW);
  uint32_t agora = millis();

  if (lido != estavaPressionado && agora - tMudanca >= DEBOUNCE_MS) {
    tMudanca = agora;
    estavaPressionado = lido;
    return lido;
  }
  return false;
}

// ======================== SENSOR ============================

void resetarImpacto() {
  impacto = Impacto();
}

// Processa uma leitura. Retorna true quando um soco acabou de terminar
// (o pico fica em impacto.pico).
bool processarSensor() {
  uint16_t valor = analogRead(SENSOR_PIN);
  uint32_t agora = millis();

  if (valor > LIMIAR_IMPACTO) {
    impacto.tUltimoAcima = agora;
    if (!impacto.ativo) {
      if (++impacto.confirmacoes >= LEITURAS_CONFIRMACAO) {
        impacto.ativo = true;
        impacto.pico = valor;
        Serial.println(">>> Impacto detectado");
      }
    } else if (valor > impacto.pico) {
      impacto.pico = valor;
    }
  } else if (!impacto.ativo) {
    impacto.confirmacoes = 0;               // ruído isolado: descarta
  }

  return impacto.ativo && (agora - impacto.tUltimoAcima >= TEMPO_FIM_IMPACTO);
}

// ================= TRANSIÇÕES DE ESTADO =====================

void entrarEstado(Estado novo) {
  jogo.estado = novo;
  jogo.tEstado = millis();

  switch (novo) {
    case Estado::PREPARANDO:
      Serial.printf("\n===== TENTATIVA %u/%u =====\n", jogo.tentativa, TOTAL_TENTATIVAS);
      exibir("Prepare-se %u/%u", jogo.tentativa, TOTAL_TENTATIVAS);
      break;

    case Estado::MEDINDO:
      resetarImpacto();
      Serial.println(">>> SOCO LIBERADO!");
      exibir("VAI!");
      break;

    case Estado::MOSTRANDO_RESULTADO: {
      uint16_t d = jogo.dano[jogo.tentativa - 1];
      Serial.printf("Tentativa %u - dano: %u\n", jogo.tentativa, d);
      exibir("Soco %u: %u %u %u", jogo.tentativa, d, d, d);
      break;
    }

    case Estado::FINALIZADO:
      jogo.melhor = 0;
      for (uint8_t i = 0; i < TOTAL_TENTATIVAS; i++)
        if (jogo.dano[i] > jogo.melhor) jogo.melhor = jogo.dano[i];

      Serial.println("\n========= FIM DO JOGO =========");
      for (uint8_t i = 0; i < TOTAL_TENTATIVAS; i++)
        Serial.printf("Tentativa %u: %u\n", i + 1, jogo.dano[i]);
      Serial.printf("MAIOR DANO: %u\n", jogo.melhor);
      exibir("Melhor: %u", jogo.melhor);
      break;
  }
}

void novaRodada() {
  memset(jogo.dano, 0, sizeof(jogo.dano));
  jogo.melhor = 0;
  jogo.tentativa = 1;
  entrarEstado(Estado::PREPARANDO);
}

void registrarSoco(uint16_t dano) {
  jogo.dano[jogo.tentativa - 1] = dano;
  entrarEstado(Estado::MOSTRANDO_RESULTADO);
}

// ===================== MÁQUINA DE ESTADOS ===================

void atualizarJogo() {
  uint32_t decorrido = millis() - jogo.tEstado;

  switch (jogo.estado) {

    case Estado::PREPARANDO:
      // FSR ignorado. Só libera quando o tempo passou E o sensor
      // está em repouso (evita contar pressão residual/encostada).
      if (decorrido >= TEMPO_PREPARACAO && analogRead(SENSOR_PIN) < LIMIAR_IMPACTO)
        entrarEstado(Estado::MEDINDO);
      break;

    case Estado::MEDINDO:
      if (processarSensor()) {
        registrarSoco(impacto.pico);   // valor bruto do ADC (0-4095)
      } else if (!impacto.ativo && decorrido >= TEMPO_LIMITE_SOCO) {
        Serial.println("Tempo esgotado: nenhum soco detectado");
        registrarSoco(0);
      }
      break;

    case Estado::MOSTRANDO_RESULTADO:
      // FSR não é lido aqui.
      if (decorrido >= TEMPO_RESULTADO) {
        if (jogo.tentativa < TOTAL_TENTATIVAS) {
          jogo.tentativa++;
          entrarEstado(Estado::PREPARANDO);
        } else {
          entrarEstado(Estado::FINALIZADO);
        }
      }
      break;

    case Estado::FINALIZADO:
      if (botaoPressionado() || decorrido >= TEMPO_REINICIO)
        novaRodada();
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

  novaRodada();
}

void loop() {
  atualizarJogo();
  atualizarScroll();
}