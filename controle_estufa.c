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
#define BUZZER_A 21
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
static volatile uint32_t last_time_display = 0; // Variável para armazenar o tempo do último evento da main
static volatile uint32_t last_time_usb = 0; // Variável para armazenar o tempo da última mensagem USB
static volatile uint32_t last_time_temp_normal = 0; // Variável para armazenar o tempo que a temperatura está normal
static volatile uint32_t last_time_umid_normal = 0; // Variável para armazenar o tempo que a umidade está normal
static volatile uint cor = 0; // Variável para armazenar a cor da borda do display


// Variáveis de configuração para os atuadores
int temp_min =  20; // Temperatura mínima
int temp_max =  35; // Temperatura máxima
volatile int16_t temp_atual; // Temperatura atual
uint umid_min =  30; // Umidade mínima
uint umid_max =  70; // Umidade máxima
volatile uint16_t umid_atual; // Umidade atual


// Variáveis para armazenar os avisos
typedef struct {
    char nivel_temp[20]; // Nível de temperatura (baixa, normal, alta)
    char nivel_umid[20]; // Nível de umidade (baixa, normal, alta)
    char string_temp_atual[6]; // Temperatura atual
    char string_umid_atual[6]; // Umidade atual
} msg_t;


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
            valor_led = matrix_rgb(30, 10, 0); // Laranja
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
    
    pwm_set_wrap(slice, max_value_joy);

    pwm_set_enabled(slice, true);  // Habilita o slice PWM
}




void iniciar_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice_num, 125);
    pwm_set_wrap(slice_num, 1000);
    pwm_set_gpio_level(pin, 10); //Para um som mais baixo
    pwm_set_enabled(slice_num, true);
}

void parar_buzzer(uint pin) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_enabled(slice_num, false);
    gpio_put(pin, 0);
}

bool repeating_timer_callback(struct repeating_timer *timer) {
    msg_t *msg = (msg_t *) timer->user_data; // Obtém a mensagem

    // Leitura dos valores do joystick
    adc_select_input(1);  
    uint16_t vrx_value = adc_read(); // Lê o valor do eixo x (Umidade)
    adc_select_input(0);  
    uint16_t vry_value = adc_read(); // Lê o valor do eixo y (Temperatura)

    umid_atual = ((vrx_value - 16)/ max_value_joy) * 100; // Converte o valor do eixo x para a faixa de 0 a 100
    temp_atual = ((vry_value - 16) / max_value_joy) * 95 - 10;  // Converte o valor do eixo y para a faixa de -10 a 85
    
    
    sprintf(msg->string_temp_atual, "%d C", temp_atual); // Formata a string
    sprintf(msg->string_umid_atual, "%u %%", umid_atual); // Formata a string
    uint32_t current_time_normal = to_ms_since_boot(get_absolute_time()); // Obtém o tempo atual em milissegundos
    
    // Verifica se a temperatura está fora do intervalo
    if (temp_atual < temp_min){
        pwm_set_gpio_level(LED_PIN_RED, 0);
        pwm_set_gpio_level(LED_PIN_BLUE, 0);
        pwm_set_gpio_level(LED_PIN_GREEN, 0);
        strcpy(msg->nivel_temp, "baixa");
    } else if (temp_atual > temp_max){
        pwm_set_gpio_level(LED_PIN_RED, max_value_joy);
        pwm_set_gpio_level(LED_PIN_BLUE, 0);
        pwm_set_gpio_level(LED_PIN_GREEN, 0);
        strcpy(msg->nivel_temp, "alta");
    } else {
        // Ajusta a intensidade do LED verde de acordo com a temperatura
        pwm_set_gpio_level(LED_PIN_RED, 0);
        pwm_set_gpio_level(LED_PIN_BLUE, 0);
        pwm_set_gpio_level(LED_PIN_GREEN, (temp_atual - temp_min) * max_value_joy / (temp_max - temp_min));
        strcpy(msg->nivel_temp, "normal"); 
        last_time_temp_normal = current_time_normal;
    }

    // Verifica se a umidade está fora do intervalo
    if (umid_atual < umid_min){
        // Ativa umidificador
        display_desenho(0); // Desenha o padrão de umidificador
        strcpy(msg->nivel_umid, "baixa");
    } else if (umid_atual > umid_max){
        // Ativa desumidificador
        display_desenho(1); // Desenha o padrão de desumidificador
        strcpy(msg->nivel_umid, "alta");
    } else{
        // Desliga umidificador e desumidificador
        display_desenho(2); // Desliga os LEDs
        strcpy(msg->nivel_umid, "normal");
        last_time_umid_normal = current_time_normal;
    }

    // Verifica se a temperatura está fora do intervalo por mais de 10 segundos
    if (current_time_normal - last_time_temp_normal > 10000 || current_time_normal - last_time_umid_normal > 10000){
        // Ativa o buzzer
        iniciar_buzzer(BUZZER_A);
    } else{
        parar_buzzer(BUZZER_A);
    }

    return true;
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



    // Configura o LED RGB como PWM
    pwm_setup(LED_PIN_RED);
    pwm_setup(LED_PIN_BLUE);
    pwm_setup(LED_PIN_GREEN);

    // Configuração do ADC
    adc_init();
    adc_gpio_init(JOY_X);
    adc_gpio_init(JOY_Y);

    stdio_init_all();


    struct repeating_timer timer;
    msg_t msg; // Cria uma variável para armazenar os avisos
    timer.user_data = &msg; // Passa a variável de avisos para o timer
    // Chama a função imediatamente antes de iniciar o timer
    repeating_timer_callback(&timer);
    
    // Configura um timer repetitivo para chamar a função de callback a cada 6 segundos
    add_repeating_timer_ms(6000, repeating_timer_callback, &msg, &timer);


    bool mostrar_nivel = false;
    uint32_t current_time_display = 1501; // Inicializa a variável de tempo para mostrar os avisos assim que iniciar o programa
    uint32_t current_time_usb = 7001; // Inicializa a variável de tempo para mostrar o menu USB assim que iniciar a conexão

    while (true){
        if (current_time_display - last_time_display > 1500){
            mostrar_nivel = !mostrar_nivel;
            last_time_display = current_time_display;
        
            if (mostrar_nivel){ 
                // Atualiza o conteúdo do display com animações
                ssd1306_fill(&ssd, true); // Limpa o display
                ssd1306_rect(&ssd, 3, 3, 122, 58, false, true); // Desenha um retângulo
                ssd1306_draw_string(&ssd, "Temperatura", 8, 10); // Desenha uma string
                ssd1306_draw_string(&ssd, msg.nivel_temp, 8, 20); // Desenha uma string
                ssd1306_line(&ssd, 0, 32, 127, 32, true); // Desenha uma linha divisória no meio da tela
                ssd1306_draw_string(&ssd, "Umidade", 8, 40); // Desenha uma string
                ssd1306_draw_string(&ssd, msg.nivel_umid, 8, 50); // Desenha uma strin
                ssd1306_send_data(&ssd); // Atualiza o display
            } else{
                // Atualiza o conteúdo do display com animações
                ssd1306_fill(&ssd, true); // Limpa o display
                ssd1306_rect(&ssd, 3, 3, 122, 58, false, true); // Desenha um retângulo
                ssd1306_draw_string(&ssd, "Temperatura", 8, 10); // Desenha uma string
                ssd1306_draw_string(&ssd, msg.string_temp_atual, 8, 20); // Desenha uma string
                ssd1306_line(&ssd, 0, 32, 127, 32, true); // Desenha uma linha divisória no meio da tela
                ssd1306_draw_string(&ssd, "Umidade", 8, 40); // Desenha uma string
                ssd1306_draw_string(&ssd, msg.string_umid_atual, 8, 50); // Desenha uma string
                ssd1306_send_data(&ssd); // Atualiza o display
            }
        }

        if (stdio_usb_connected()){
            if (current_time_usb - last_time_usb > 7000){
                printf("\nMenu de configuração\n");
                printf("1 - Configurar temperatura mínima\n");
                printf("2 - Configurar temperatura máxima\n");
                printf("3 - Configurar umidade mínima\n");
                printf("4 - Configurar umidade máxima\n\n");
                
                printf("Temperatura: %d C\n", temp_atual);
                printf("Umidade: %u %%\n", umid_atual);
                printf("Nível de Temperatura: %s\n", msg.nivel_temp);
                printf("Nível de Umidade: %s\n", msg.nivel_umid);
                printf("Sua escolha: ");
                last_time_usb = current_time_usb;
            }
            char choice = getchar_timeout_us(1); // Lê a escolha do usuário em 100ms
            if (choice == '1'){
                printf("Temperatura mínima atual: %d C\n", temp_min);
                printf("Digite a nova temperatura mínima: ");
                scanf("%d", &temp_min);
                printf("Configurado\n");
            } else if (choice == '2'){
                printf("Temperatura máxima: %d C\n", temp_max);
                printf("Digite a nova temperatura máxima: ");
                scanf("%d", &temp_max);
                printf("Configurado\n");
            } else if (choice == '3'){
                printf("Umidade mínima atual: %u %%\n", umid_min);
                printf("Digite a nova umidade mínima: ");
                scanf("%u", &umid_min);
                printf("Configurado\n");
            } else if (choice == '4'){
                printf("Umidade máxima atual: %u %%\n", umid_max);
                printf("Digite a umidade máxima atual: ");
                scanf("%u", &umid_max);
                printf("Configurado\n");
            }
            current_time_usb = to_ms_since_boot(get_absolute_time());
        }
        
        current_time_display = to_ms_since_boot(get_absolute_time());
    }

    return 0;
}