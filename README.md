# PJI2: Soco Eletrônico

Repositório da disciplina de Projeto Integrador II para armazenamento de códigos/testes para funções do ESP32.

Desenvolvimento de um soco eletrônico a ser implantado na feira de jogos do IFSC câmpus São José.


# Alunos: Igor da Silva e Tiago Cargnin Morandi.
# Disciplina: Projeto Integrador II - 129006.


# Pinagem da matriz de LEDs MAX7219:

- VCC => VIN;
- GND => GND;
- CLK => PIN 18;
- DIN => PIN 23;
- CS  => PIN 5.


# Pinagem do circuito Divisor de Tensão:

- 3V3 <---> FSR <---> NÓ_A <---> Potenciometro (2k2) <---> GND
- NÓ_A <---> PIN 34


# Pinagem do adaptador de cartão microSD:

- CS => PIN 4;
- SCK => PIN 18;
- MOSI => PIN 23;
- MISO => PIN 19;
- VCC => VIN (3V3 não funcionou);
- GND => GND.

# Dependências para instalar na IDE do arduíno:

## Boards Managar:

- Esp32 by Espressif Systems


## Bibliotecas:

- MD_MAX72XX by majicDesigns

## Boards:

- ESP32 DEV MODULE
