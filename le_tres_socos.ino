// Código para ler 3 socos e imprimir as respectivas pontuacoes
// na matriz MAX7219
// uso de maquina de estados

#include <MD_MAX72xx.h>
#include <SPI.h>

// ========== configuracao da matriz ============

#define IMMEDIATE_NEW   1     // if 1 will immediately display a new message
#define USE_POT_CONTROL 0
#define PRINT_CALLBACK  0

#define PRINT(s, v) { Serial.print(F(s)); Serial.print(v); }

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
//#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define CLK_PIN   18  // or SCK
#define DATA_PIN  23  // or MOSI
#define CS_PIN    5  // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// =========== configuracao de scrolling ==============

#if USE_POT_CONTROL
#define SPEED_IN  34
#else
#define SCROLL_DELAY  75  // in milliseconds
#endif // USE_POT_CONTROL

#define CHAR_SPACING  1 // pixels between characters

// Global message buffers shared by Serial and Scrolling functions
#define BUF_SIZE  75
uint8_t curMessage[BUF_SIZE] = { "Sua vez! " };
uint8_t newMessage[BUF_SIZE];
bool newMessageAvailable = false;

uint16_t  scrollDelay;  // in milliseconds

// ================== configuracao do FSR ===================
#define SENSOR 34
#define LIMIAR_IMPACTO 500
#define TEMPO_FIM_IMPACTO 100

bool emImpacto = false;
int maiorImpacto = 0;
uint32_t ultimoImpacto = 0;

// ================= configuracao do jogo =====================
#define TOTAL_TENTATIVAS 3

// tempo mostrando "Tentativa i" ou "Vai, sua vez" antes de liberar o soco
#define TEMPO_PREPARACAO 2000

// tempo mostrando o resultado antes de liberar a prox tentativa
#define TEMPO_RESULTADO 10000

enum EstadoJogo{
  PREPARANDO,
  MEDINDO,
  MOSTRANDO_RESULTADO,
  FINALIZADO
};

EstadoJogo estadoJogo = PREPARANDO;
int tentativaAtual = 0;
int pontos[TOTAL_TENTATIVAS] = {0,0,0};
uint32_t tempoEstado = 0;

void mostrarMensagem(const char *mensagem){
  snprintf((char*)newMessage, BUF_SIZE, "%s  ", mensagem);

  newMessageAvailable = true;
}

void mostrarPontuacao(int valor)
{
  snprintf((char*)newMessage, BUF_SIZE, "Sua pontuacao: %d   ", valor);
  newMessageAvailable = true;
}

// ao final das jogadas
void mostrarTodosPontos(){
  snprintf((char*)newMessage, BUF_SIZE, "Pontos: %d %d %d  ",
           pontos[0], pontos[1], pontos[2]);

  newMessageAvailable = true;
}

// ==============================================================

void readSerial(void)
{
  static uint8_t  putIndex = 0;

  while (Serial.available())
  {
    newMessage[putIndex] = (char)Serial.read();
    if ((newMessage[putIndex] == '\n') || (putIndex >= BUF_SIZE-3)) // end of message character or full buffer
    {
      // put in a message separator and end the string
      newMessage[putIndex++] = ' ';
      newMessage[putIndex] = '\0';
      // restart the index for next filling spree and flag we have a message waiting
      putIndex = 0;
      newMessageAvailable = true;
    }
    else if (newMessage[putIndex] != '\r')
      // Just save the next char in next location
      putIndex++;
  }
}

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
// Callback function for data that is being scrolled off the display
{
#if PRINT_CALLBACK
  Serial.print("\n cb ");
  Serial.print(dev);
  Serial.print(' ');
  Serial.print(t);
  Serial.print(' ');
  Serial.println(col);
#endif
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
// Callback function for data that is required for scrolling into the display
{
  static uint8_t* p = curMessage;
  static enum { NEW_MESSAGE, LOAD_CHAR, SHOW_CHAR, BETWEEN_CHAR } state = LOAD_CHAR;
  static uint8_t  curLen, showLen;
  static uint8_t  cBuf[15];
  uint8_t colData = 0;    // blank column is the default

#if IMMEDIATE_NEW
  if (newMessageAvailable)  // there is a new message waiting
  {
    state = NEW_MESSAGE;
    mx.clear(); // clear the display
  }
#endif

  // finite state machine to control what we do on the callback
  switch(state)
  {
    case NEW_MESSAGE:   // Load the new message
      memcpy(curMessage, newMessage, BUF_SIZE); // copy it in
      newMessageAvailable = false;    // used it!
      p = curMessage;
      state = LOAD_CHAR;
      break;

    case LOAD_CHAR: // Load the next character from the font table
      showLen = mx.getChar(*p++, sizeof(cBuf)/sizeof(cBuf[0]), cBuf);
      curLen = 0;
      state = SHOW_CHAR;

      // if we reached end of message, opportunity to load the next
      if (*p == '\0')
      {
        p = curMessage;     // reset the pointer to start of message
#if !IMMEDIATE_NEW
        if (newMessageAvailable)  // there is a new message waiting
        {
          state = NEW_MESSAGE;    // we will load it here
          break;
        }
#endif
      }
      // !! deliberately fall through to next state to start displaying

    case SHOW_CHAR: // display the next part of the character
      colData = cBuf[curLen++];
      if (curLen == showLen)
      {
        showLen = CHAR_SPACING;
        curLen = 0;
        state = BETWEEN_CHAR;
      }
      break;

    case BETWEEN_CHAR: // display inter-character spacing (blank columns)
      colData = 0;
      curLen++;
      if (curLen == showLen)
        state = LOAD_CHAR;
      break;

    default:
      state = LOAD_CHAR;
  }

  return(colData);
}

// atualiza o scrolling
 void scrollText(void)
{
  static uint32_t prevTime = 0;

  // Is it time to scroll the text?
  if (millis()-prevTime >= scrollDelay)
  {
    mx.transform(MD_MAX72XX::TSL);  // scroll along - the callback will load all the data
    //mx.transform(MD_MAX72XX::TSR); // para mudar fluxo de dados
    prevTime = millis();      // starting point for next time
  }
}

// velocidade do scrolling
uint16_t getScrollDelay(void)
{
#if USE_POT_CONTROL
  uint16_t  t;

  t = analogRead(SPEED_IN);
  t = map(t, 0, 1023, 25, 250);

  return(t);
#else
  return(SCROLL_DELAY);
#endif
}

// ========= preparar nova tentativa ==============
void iniciarTentativa(){

  tentativaAtual++;
  Serial.println();
  Serial.print("========== TENTATIVA ");
  Serial.print(tentativaAtual);
  Serial.println(" ==========");

  char mensagem[BUF_SIZE];

  snprintf(mensagem, BUF_SIZE, "Tentativa %d - Liberado!", tentativaAtual);

  mostrarMensagem(mensagem);
  tempoEstado = millis();
  estadoJogo = PREPARANDO;

}

// ============ Finalizar Impacto ============
void finalizarImpacto(){
  emImpacto = false; // nao está mais lendo soco

  pontos[tentativaAtual - 1] = maiorImpacto;

  Serial.print("Tentativa ");
  Serial.print(tentativaAtual);

  Serial.print(" - Maior impacto: ");
  Serial.println(maiorImpacto);


  // Mostra o resultado
  mostrarPontuacao(maiorImpacto);
  tempoEstado = millis();
  estadoJogo = MOSTRANDO_RESULTADO;
}

void setup()
{
  Serial.begin(57600);
  Serial.println();
  Serial.println("=================================");
  Serial.println("     MEDIDOR DE IMPACTO FSR");
  Serial.println("=================================");

  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);

#if USE_POT_CONTROL
  pinMode(SPEED_IN, INPUT);
#else
  scrollDelay = SCROLL_DELAY;
#endif

  newMessage[0] = '\0';

  iniciarTentativa();
}

void loop()
{
  uint32_t agora = millis();

  // ESTADO: PREPARANDO
  if (estadoJogo == PREPARANDO)
  {

    // Durante os 2 segundos de preparação,
    // o FSR NÃO é considerado.
    //
    // Isso evita que a pessoa dê um soco antes da hora.

    if (agora - tempoEstado >= TEMPO_PREPARACAO)
    {
      Serial.println(">>> SOC0 LIBERADO!");

      Serial.println("Aguardando impacto...");

      maiorImpacto = 0;

      emImpacto = false;

      estadoJogo = MEDINDO;
    }
  }

  // ESTADO: MEDINDO
  else if (estadoJogo == MEDINDO)
  {

    // Leitura do FSR
    int valor = analogRead(SENSOR);

    // Detectou início de impacto
    if (valor > LIMIAR_IMPACTO)
    {

      if (!emImpacto)
      {
        emImpacto = true;

        maiorImpacto = valor;

        Serial.println(">>> IMPACTO DETECTADO!");
      }

      // Guarda o maior valor
      if (valor > maiorImpacto)
      {
        maiorImpacto = valor;
      }

      // Atualiza o momento da última leitura acima do limiar
      ultimoImpacto = agora;
    }

    // Verifica se o impacto terminou
    if (
      emImpacto &&
      (agora - ultimoImpacto >= TEMPO_FIM_IMPACTO)
    )
    {
      finalizarImpacto();
    }
  }

  // ESTADO: MOSTRANDO RESULTADO
  else if (estadoJogo == MOSTRANDO_RESULTADO)
  {

    // Enquanto estiver mostrando o resultado,
    // o FSR NÃO é lido.
    //
    // Portanto, a pessoa não consegue gerar outro ponto
    // acidentalmente.

    if (agora - tempoEstado >= TEMPO_RESULTADO)
    {

      // Ainda existem tentativas?
      if (tentativaAtual < TOTAL_TENTATIVAS)
      {
        iniciarTentativa();
      }

      // Terminou as 3 tentativas
      else
      {
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


        // Mostra os três resultados
        mostrarTodosPontos();

        estadoJogo = FINALIZADO;
      }
    }
  }

  // ESTADO: FINALIZADO
  else if (estadoJogo == FINALIZADO)
  {
    // Não faz mais leituras do FSR.
    //
    // O display continua mostrando os resultados.
  }

  // DISPLAY
  scrollDelay = getScrollDelay();

  scrollText();
}