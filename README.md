# Sistema para Controle e Monitoramento de Estufas

## Descrição do Projeto

Este projeto tem como objetivo desenvolver um sistema embarcado que monitora e controla os parâmetros ambientais de uma estufa, mantendo a temperatura e a umidade dentro dos valores configurados pelo usuário. O sistema aciona atuadores como umidificador, desumidificador, ventilador e buzzer para garantir as condições ideais para o crescimento das plantas.

## Funcionalidades

- **Monitoramento Ambiental:**  
  Leitura dos valores de temperatura e umidade simulados por joysticks, com conversão para faixas reais (-10°C a 85°C e 0% a 100%, respectivamente).

- **Exibição dos Dados:**  
  Os valores e seus respectivos níveis (baixa, normal, alta) são exibidos em um display OLED SSD1306 e também enviados via USB UART para monitoramento pelo terminal serial.

- **Controle de Atuadores:**  
  - **LED RGB:** Indica o nível de temperatura (intensidade variável do verde ou acendimento em vermelho para temperaturas elevadas).  
  - **Matriz de LEDs:** Exibe ícones representando o acionamento do umidificador ou desumidificador.  
  - **Buzzer:** Emite um alarme sonoro caso as condições ambientais permaneçam fora dos limites configurados por mais de 10 segundos.

- **Interface de Configuração:**  
  Permite que o usuário ajuste os parâmetros de temperatura e umidade através de um menu exibido via USB UART, sem a necessidade de reprogramar o firmware.

## Especificação de Hardware

- **Microcontrolador:** Raspberry Pi Pico W (125 MHz, com suporte a I2C, PWM, PIO e comunicação USB UART)
- **Sensores:**  
  Simulação via joysticks:
  - Joystick Eixo X (GP27) para umidade
  - Joystick Eixo Y (GP26) para temperatura
- **Display:** OLED SSD1306 (128x64 pixels, I2C - pinos GP14 e GP15)
- **Atuadores:**  
  - Matriz de LEDs 5x5 (WS2818B, controle via PIO - GP7)
  - LED RGB (controle via PWM – pinos GP11 para verde, GP12 para azul, GP13 para vermelho)
  - Buzzer (controle via PWM – pino GP21)
- **Comunicação:** USB UART para interação com o usuário e configuração dos parâmetros.

## Especificação do Firmware

O firmware foi desenvolvido em C utilizando o SDK do Raspberry Pi Pico e integra os seguintes módulos:

- **ADC:** Para leitura dos valores dos joysticks.
- **PWM:** Para controle do LED RGB e do buzzer.
- **I2C:** Para comunicação com o display OLED.
- **PIO:** Para o controle da matriz de LEDs.
- **UART USB:** Para exibição de dados e menu de configuração via terminal serial.
- **Timers:** Para atualização periódica dos dados e acionamento do alarme sonoro.


## Vídeo de Demonstração
Assista à demonstração do projeto em execução:  
[Vídeo Demonstrativo](https://youtu.be/slr7jHzEJRI)
