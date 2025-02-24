#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "inc/ssd1306.h"
#include "inc/font.h"

#include <stdio.h>
#include <hardware/pio.h>           
#include "hardware/clocks.h"        
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "pico/bootrom.h"

#include "animacao_matriz.pio.h" // Biblioteca PIO para controle de LEDs WS2818B

// Definição de constantes
#define LED_PIN_GREEN 11
#define LED_PIN_BLUE 12
#define LED_PIN_RED 13
#define LED_COUNT 25            // Número de LEDs na matriz
#define MATRIZ_PIN 7            // Pino GPIO conectado aos LEDs WS2818B

#define JOY_X 27 // Joystick está de lado em relação ao que foi dito no pdf
#define JOY_Y 26
#define SW_PIN 22
#define BUTTON_PIN_A 5          // Pino GPIO conectado ao botão A
#define zona_morta 100
#define max_value_joy 4065.0 // (4081 - 16) que são os valores extremos máximos lidos pelo meu joystick

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C


// Declaração de variáveis globais
PIO pio;
uint sm;
ssd1306_t ssd; // Inicializa a estrutura do display
static volatile uint32_t last_time = 0; // Variável para armazenar o tempo do último evento
static volatile uint green_state = 0; // Variável para armazenar o estado do LED verde
static volatile uint led_pwm = 1; // Variável para habilitar/desabilitar o controle PWM dos LEDs
static volatile uint cor = 0; // Variável para armazenar a cor da borda do display

// Variáveis de configuração para os atuadores
int temp_min =  20; // Temperatura mínima
int temp_max =  35; // Temperatura máxima
uint umid_min =  30; // Umidade mínima
uint umid_max =  70; // Umidade máxima
int fan_med =  50; // Velocidade média do ventilador
uint bomb_time = 30000; // Tempo de acionamento da bomba
uint bomb_duration = 5000; // Duração do acionamento da bomba


uint padrao_led[10][LED_COUNT] = {
    {0, 0, 1, 0, 0,
     0, 1, 1, 1, 0,
     1, 1, 1, 1, 1,
     1, 1, 1, 1, 1,
     0, 1, 1, 1, 0,
    }, // Umidificador Ativo (Desenho de gota)
    {2, 0, 0, 2, 0,
     0, 2, 0, 0, 2,
     2, 0, 0, 2, 0,
     0, 2, 0, 0, 2,
     2, 0, 0, 2, 0,
    }, // Desumidificador ativo (Desenho de Seco)
    {1, 0, 1, 0, 1,
     1, 0, 1, 0, 1,
     0, 1, 1, 1, 0,
     0, 1, 1, 1, 0,
     0, 0, 1, 0, 0,
    }, // Bomba de água ativa (Desenho de respingos pra cima)
    {0, 0, 1, 0, 0,
     0, 1, 1, 1, 0,
     1, 1, 1, 1, 1,
     1, 1, 1, 1, 1,
     0, 1, 1, 1, 0,
    },
    {0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,
     0, 0, 0, 0, 0,
    } // Desliga os LEDs
};

// Ordem da matriz de LEDS, útil para poder visualizar na matriz do código e escrever na ordem correta do hardware
int ordem[LED_COUNT] = {0, 1, 2, 3, 4, 9, 8, 7, 6, 5, 10, 11, 12, 13, 14, 19, 18, 17, 16, 15, 20, 21, 22, 23, 24};  


//rotina para definição da intensidade de cores do led
uint32_t matrix_rgb(unsigned r, unsigned g, unsigned b){
    // Para não ficar forte demais, a intensidade de cor é multiplicada por 50
    return (g << 24) | (r << 16) | (b << 8);
}

void display_desenho(int number){
    uint32_t valor_led;

    for (int i = 0; i < LED_COUNT; i++){
        // Define a cor do LED de acordo com o padrão
        if (padrao_led[number][ordem[24 - i]] == 1){
            valor_led = matrix_rgb(0, 0, 10); // Azul
        } else if (padrao_led[number][ordem[24 - i]] == 2){
            valor_led = matrix_rgb(30, 10, 0); // 
        } else if (padrao_led[number][ordem[24 - i]] == 3){
            valor_led = matrix_rgb(1, 0, 0);
        } else{
            valor_led = matrix_rgb(0, 0, 0); // Desliga o LED
        }
        // Atualiza o LED
        pio_sm_put_blocking(pio, sm, valor_led);
    }
}

void pwm_setup(uint pino) {
    gpio_set_function(pino, GPIO_FUNC_PWM);         // Configura o pino como saída PWM
    uint slice = pwm_gpio_to_slice_num(pino);         // Obtém o slice correspondente

    // Configura o divisor de clock:
    //pwm_set_clkdiv(slice, 4.0);
    
    pwm_set_wrap(slice, max_value_joy);

    pwm_set_enabled(slice, true);  // Habilita o slice PWM
}


void configuraGPIO(){
    // Configuração do LED RGB
    pwm_setup(LED_PIN_RED);
    pwm_setup(LED_PIN_BLUE);
    pwm_setup(LED_PIN_GREEN);



    // Configura os botões
    gpio_init(BUTTON_PIN_A);
    gpio_set_dir(BUTTON_PIN_A, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_A);

    gpio_init (SW_PIN);
    gpio_set_dir(SW_PIN, GPIO_IN);
    gpio_pull_up(SW_PIN);
}



static void gpio_irq_handler(uint gpio, uint32_t events) {
    // Obtém o tempo atual em milissegundos
   uint32_t current_time = to_ms_since_boot(get_absolute_time());
   // Verificação de tempo para debounce
   if (current_time - last_time > 200){
        if(gpio == BUTTON_PIN_A){
            // Entra no modo bootsel
            reset_usb_boot(0, 0);

        } else if (gpio == SW_PIN){
            green_state = !green_state;
            cor = !cor;
            gpio_put(LED_PIN_GREEN, green_state);

        }

       last_time = current_time; // Atualiza o tempo do último evento
   }
}


int main(){
    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA); // Pull up the data line
    gpio_pull_up(I2C_SCL); // Pull up the clock line
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd); // Configura o display
    ssd1306_send_data(&ssd); // Envia os dados para o display

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Configuração do PIO
    pio = pio0; 
    uint offset = pio_add_program(pio, &animacao_matriz_program);
    sm = pio_claim_unused_sm(pio, true);
    animacao_matriz_program_init(pio, sm, offset, MATRIZ_PIN);



    // Configura os LEDs e botões
    configuraGPIO();
    // Configuração do ADC
    adc_init();
    adc_gpio_init(JOY_X);
    adc_gpio_init(JOY_Y);


    // Configuração da interrupção
    gpio_set_irq_enabled_with_callback(BUTTON_PIN_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(SW_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);


    stdio_init_all();

    bool mostrar_avisos = false;

    while (true){
        // Leitura dos valores do joystick
        adc_select_input(1);  
        uint16_t vrx_value = adc_read(); // Lê o valor do eixo x (Umidade)
        adc_select_input(0);  
        uint16_t vry_value = adc_read(); // Lê o valor do eixo y (Temperatura)

        uint16_t umid_atual = (abs(vrx_value - 2048) > zona_morta) ? ((vrx_value - 16)/ max_value_joy) * 100 : 50; // Converte o valor do eixo x para a faixa de 0 a 100
        int16_t temp_atual =  ((vry_value - 16) / max_value_joy) * 95 - 10;  // Converte o valor do eixo y para a faixa de -10 a 85


        char avisoT[20] = "normal"; // String para armazenar o aviso de temperatura
        char avisoU[20] = "normal"; // String para armazenar o aviso de umidade

        // Verifica se a temperatura está fora do intervalo
        if (temp_atual < temp_min){
            pwm_set_gpio_level(LED_PIN_RED, 0);
            pwm_set_gpio_level(LED_PIN_BLUE, 0);
            pwm_set_gpio_level(LED_PIN_GREEN, 0);
            strcpy(avisoT, "baixa");
        } else if (temp_atual > temp_max){
            pwm_set_gpio_level(LED_PIN_RED, max_value_joy);
            pwm_set_gpio_level(LED_PIN_BLUE, 0);
            pwm_set_gpio_level(LED_PIN_GREEN, 0);
            strcpy(avisoT, "alta");
        } else {
            // Ajusta a intensidade do LED verde de acordo com a temperatura
            pwm_set_gpio_level(LED_PIN_RED, 0);
            pwm_set_gpio_level(LED_PIN_BLUE, 0);
            pwm_set_gpio_level(LED_PIN_GREEN, (temp_atual - temp_min) * max_value_joy / (temp_max - temp_min));
        }

        // Verifica se a umidade está fora do intervalo
        if (umid_atual < umid_min){
            // Ativa umidificador
            display_desenho(0); // Desenha o padrão de umidificador
            strcpy(avisoU, "baixa");
        } else if (umid_atual > umid_max){
            // Ativa desumidificador
            display_desenho(1); // Desenha o padrão de desumidificador
            strcpy(avisoU, "alta");
        } else{
            // Desliga umidificador e desumidificador
            display_desenho(4); // Desliga os LEDs
        }

        // Mostra a temperatura e umidade no display
        char stringT[5]; // String para armazenar a temperatura
        char stringU[5]; // String para armazenar a umidade
        
        sprintf(stringT, "%d C", temp_atual); // Formata a string
        sprintf(stringU, "%u %%", umid_atual); // Formata a string

        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_time > 5000){
            mostrar_avisos = !mostrar_avisos;
            last_time = current_time;
        }

        if (mostrar_avisos){ 
            // Atualiza o conteúdo do display com animações
            ssd1306_fill(&ssd, true); // Limpa o display
            ssd1306_rect(&ssd, 3, 3, 122, 58, false, true); // Desenha um retângulo
            ssd1306_draw_string(&ssd, "Temperatura", 8, 10); // Desenha uma string
            ssd1306_draw_string(&ssd, avisoT, 8, 20); // Desenha uma string
            ssd1306_line(&ssd, 0, 32, 127, 32, true); // Desenha uma linha divisória no meio da tela
            ssd1306_draw_string(&ssd, "Umidade", 8, 40); // Desenha uma string
            ssd1306_draw_string(&ssd, avisoU, 8, 50); // Desenha uma strin
            ssd1306_send_data(&ssd); // Atualiza o display
        } else{
            // Atualiza o conteúdo do display com animações
            ssd1306_fill(&ssd, true); // Limpa o display
            ssd1306_rect(&ssd, 3, 3, 122, 58, false, true); // Desenha um retângulo
            ssd1306_draw_string(&ssd, "Temperatura", 8, 10); // Desenha uma string
            ssd1306_draw_string(&ssd, stringT, 8, 20); // Desenha uma string
            ssd1306_line(&ssd, 0, 32, 127, 32, true); // Desenha uma linha divisória no meio da tela
            ssd1306_draw_string(&ssd, "Umidade", 8, 40); // Desenha uma string
            ssd1306_draw_string(&ssd, stringU, 8, 50); // Desenha uma string
            ssd1306_send_data(&ssd); // Atualiza o display
            
            printf("Temperatura: %d C\n", temp_atual);
            printf("Umidade: %u %%\n", umid_atual);
        }
        

        


        sleep_ms(100); 
    }

    return 0;
}