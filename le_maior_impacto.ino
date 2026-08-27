// Código que monitora o sensor e imprime o valor lido (entre 0 e 4095)
// no display serial;
// Guarda o maior impacto lido e o atualiza se um próximo valor lido o superar

#define SENSOR 34

int maiorImpacto = 0;

void setup() {
  Serial.begin(115200); // visualizar na serial
}

void loop() {

  int valor = analogRead(SENSOR);

  if(valor > maiorImpacto){ 
    maiorImpacto = valor;
  }

  // comparando o soco atual com o maior valor
  Serial.print("Atual: ");
  Serial.print(valor);
  
  Serial.print("  Maior: ");
  Serial.println(maiorImpacto);

  delay(250);
}