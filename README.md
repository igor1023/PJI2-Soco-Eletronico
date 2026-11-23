# PJI2: Soco Eletrônico

Repositório da disciplina de Projeto Integrador II para armazenamento de códigos/testes para funções do ESP32 | Engenharia de Telecomunicações | IFSC SJE.

Desenvolvimento de um soco eletrônico a ser implantado na feira de jogos do IFSC câmpus São José.


# Alunos: Igor da Silva e Tiago Cargnin Morandi.
# Disciplina: Projeto Integrador II - 129006.


# Pinagem da matriz de LEDs MAX7219:

- VCC => Positivo da Fonte Externa;
- GND => Negativo da Fonte Externa (que é conectado ao GND da ESP32);
- CLK => PIN 18;
- DIN => PIN 23;
- CS  => PIN 5.


# Pinagem do circuito Divisor de Tensão:

- 3V3 <---> FSR <---> NÓ_A <---> Potenciometro <---> GND;
- NÓ_A <---> PIN 34.


# Pinagem do Buzzer:
- VCC => 3V3;
- GND => GND;
- I/O => PIN 25.


# Pinagem do Botão (Push Button):
- Um terminal => PIN 26;
- Outro terminal => GND.
  

# Dependências para instalar na IDE do arduíno:

## Boards Managar:

- Esp32 by Espressif Systems


## Bibliotecas:

- MD_MAX72XX by majicDesigns

## Boards:

- ESP32 DEV MODULE
