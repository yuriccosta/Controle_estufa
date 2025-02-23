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

    gpio_init(LED_PIN_GREEN);
    gpio_set_dir(LED_PIN_GREEN, GPIO_OUT);
    gpio_put(LED_PIN_GREEN, green_state);

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
           led_pwm = !led_pwm;
           printf("PWM alterado para %u\n", led_pwm);  

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

    while (true){
        // Leitura dos valores do joystick
        adc_select_input(1);  
        uint16_t vrx_value = adc_read(); 

        adc_select_input(0);  
        uint16_t vry_value = adc_read(); 

        // Verifica se o controle PWM está habilitado
        if (led_pwm){
            // Aumenta os valores do pwm ao se aproximar dos extremos do eixo x e y
            uint16_t pwm_x = (abs(vrx_value - 2048) > zona_morta) ? abs(vrx_value - 2048) * 2 : 0;
            uint16_t pwm_y = (abs(vry_value - 2048) > zona_morta) ? abs(vry_value - 2048) * 2 : 0;
            
            // Atualiza o valor do PWM
            pwm_set_gpio_level(LED_PIN_RED, pwm_x); 
            pwm_set_gpio_level(LED_PIN_BLUE, pwm_y);
        
        }
        
        // x e y são o centro do quadrado
        uint16_t x = (vrx_value * WIDTH) / max_value_joy; // Calcula a posição do eixo x 
        uint16_t y = HEIGHT - ((vry_value * HEIGHT) / max_value_joy); // Calcula a posição do eixo y

        // Limita a posição do quadrado para não ultrapassar as bordas do retangulo
        if (x > 120) x = 120;
        if (x < 8) x = 8;
        if (y > 56) y = 56;
        if (y < 8) y = 8;

        x = x - 4; // Ajusta a posição do quadrado para passar para a função de desenho
        y = y - 4; // Ajusta a posição do quadrado para passar para a função de desenho
        
        ssd1306_fill(&ssd, cor); // Limpa o display
        ssd1306_rect(&ssd, 3, 3, 122, 58, !cor, cor); // Desenha um retângulo
        ssd1306_rect(&ssd, y, x, 8, 8, true, true); // Desenha um quadrado
        ssd1306_send_data(&ssd); // Atualiza o display

        sleep_ms(100); 
    }

    return 0;
}