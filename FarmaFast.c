/*Diego da Silva Campos do Nascimento - Diegocamposrj
Projeto Final-FarmaFast
EmbarcaTech 2024/2025
*/
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

// Definições de pinos
#define TRIG_PIN 4          // Sensor HC-SR04 TRIG
#define ECHO_PIN 8          // Sensor HC-SR04 ECHO
#define SDA_PIN 0           // LCD I2C SDA
#define SCL_PIN 1           // LCD I2C SCL
#define UP_BUTTON_PIN 5     // Botão UP
#define DOWN_BUTTON_PIN 6   // Botão DOWN
#define CONFIRM_BUTTON_PIN 22 // Botão CONFIRM
#define SERVO_PIN 9         // Servo motor

// Constantes
#define DISTANCE_THRESHOLD 20   // cm - Distância limite para detecção de presença
#define SUS_CODE "233322332"    // Código SUS válido
#define PASSWORD "2233"         // Senha válida

// Definições do LCD I2C
#define I2C_PORT i2c0       // Porta I2C utilizada
#define LCD_ADDRESS 0x27    // Endereço I2C do LCD
#define LCD_DELAY_US 2000   // Tempo de atraso para comandos do LCD


// Variáveis globais
int medicine_count = 10; // Quantidade inicial de remédios
char keypad_input[16];   // Buffer para entrada do teclado virtual
uint8_t backlight_state = 0x08; // Ligado por padrão - Estado do backlight do LCD

// Protótipos das funções
void lcd_send_byte(uint8_t data, uint8_t rs);
void lcd_init();
void lcd_clear();
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_write_char(char c);
void lcd_write_string(const char* str);
void init_peripherals();
bool detect_presence();
void display_message(const char* message, bool scroll);
char* virtual_keypad(int max_length, int exact_length); // Teclado virtual com comprimento exato
char* virtual_keypad_with_msg(const char* msg, int cursor_col, int max_length, int exact_length); // Função corrigida
bool verify_sus(const char* input);
bool verify_password(const char* input);
void display_menu(int* selected);
int select_medicine();
void release_medicine();
void check_presence_and_sleep();

// Funções do LCD
void lcd_send_byte(uint8_t data, uint8_t rs){
     // Envia um byte para o LCD através do I2C
    // rs: 0 para comando, 1 para dado
    uint8_t buf[4];
    uint8_t high_nibble = (data & 0xF0) | rs | backlight_state; // Nibble superior
    uint8_t low_nibble = ((data << 4) & 0xF0) | rs | backlight_state; // Nibble inferior

    buf[0] = high_nibble | 0x04; // Habilita o enable (E)
    buf[1] = high_nibble; // Desabilita o enable (E)
    buf[2] = low_nibble | 0x04; // Habilita o enable (E)
    buf[3] = low_nibble; // Desabilita o enable (E)

    i2c_write_blocking(I2C_PORT, LCD_ADDRESS, buf, 4, false); // Escreve os nibbles
    sleep_us(LCD_DELAY_US); // Aguarda o atraso
}
void lcd_init() {
    // Inicializa o LCD I2C
    i2c_init(I2C_PORT, 100 * 1000); // Inicializa a comunicação I2C
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C); // Define os pinos SDA e SCL para I2C
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN); // Pull-up nos pinos I2C devedo a uma ruido dutante os teste
    gpio_pull_up(SCL_PIN);

    sleep_ms(50); // Atraso de inicialização
     // Sequência de inicialização do LCD (datasheet)
    lcd_send_byte(0x03 << 4, 0); sleep_ms(5);
    lcd_send_byte(0x03 << 4, 0); sleep_us(150);
    lcd_send_byte(0x03 << 4, 0);
    lcd_send_byte(0x02 << 4, 0);

    lcd_send_byte(0x28, 0); // Função set
    lcd_send_byte(0x0C, 0); // Display on/off control
    lcd_send_byte(0x01, 0); // Limpa o display
    lcd_send_byte(0x06, 0); // Entry mode set
}

void lcd_clear() {
    lcd_send_byte(0x01, 0);
    sleep_ms(2);
}

void lcd_set_cursor(uint8_t col, uint8_t row) {
    uint8_t address = (row == 0 ? 0x00 : 0x40) + col;
    lcd_send_byte(0x80 | address, 0);
}

void lcd_write_char(char c) {
    lcd_send_byte(c, 1);
}

void lcd_write_string(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        lcd_write_char(str[i]);
    }
}
void lcd_backlight(bool on) { //uso na economia da energia utilizada pelo LCD
    backlight_state = on ? 0x08 : 0x00; // 0x08 = ligado, 0x00 = desligado
    lcd_send_byte(0x00, 0); // Envia um comando dummy para atualizar o backlight
}

void display_message(const char* message, bool scroll) {
    if (scroll) {
        int len = strlen(message);
        if (len <= 16) {
            lcd_set_cursor(0, 0);
            lcd_write_string(message);
            sleep_ms(5000); // Exibe por 5 segundos
        } else {
            char line1[17];
            char line2[17];
            strncpy(line1, message, 16);
            line1[16] = '\0';
            strncpy(line2, message + 16, len - 16);
            line2[len - 16] = '\0';

            lcd_set_cursor(0, 0);
            lcd_write_string(line1);
            lcd_set_cursor(0, 1);
            lcd_write_string(line2);
            sleep_ms(5000); // Exibe por 5 segundos
        }
    } else {
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_write_string(message);
    }
}

void init_peripherals() {
    // Inicializa os periféricos: LCD, servo, botões e sensor HC-SR04
    lcd_init(); // Inicializa o LCD
    // Inicializa o servo motor
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM); // Define o pino do servo como PWM
    uint slice_num = pwm_gpio_to_slice_num(SERVO_PIN);
    pwm_set_wrap(slice_num, 20000); // Define o período do PWM (50Hz)
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 1500); // Define o nível inicial do PWM (posição de repouso do servo)
    pwm_set_enabled(slice_num, true);// Habilita o PWM

    // Configuração dos botões com pull-up interno
    gpio_init(UP_BUTTON_PIN); // Inicializa os pinos dos botões
    gpio_set_dir(UP_BUTTON_PIN, GPIO_IN); // Define como entrada
    gpio_pull_up(UP_BUTTON_PIN); // Habilita o pull-up interno
    //configuração dos botões DOWN e CONFIRM
    gpio_init(DOWN_BUTTON_PIN);
    gpio_set_dir(DOWN_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(DOWN_BUTTON_PIN);

    gpio_init(CONFIRM_BUTTON_PIN);
    gpio_set_dir(CONFIRM_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(CONFIRM_BUTTON_PIN);

    // Configuração do sensor HC-SR04
    gpio_init(TRIG_PIN); // Inicializa os pinos do sensor
    gpio_set_dir(TRIG_PIN, GPIO_OUT); // Define TRIG como saída
    gpio_init(ECHO_PIN); // Inicializa os pinos do sensor
    gpio_set_dir(ECHO_PIN, GPIO_IN); // Define ECHO como entrada
}

bool detect_presence() {
    // 1. Dispara o pulso ultrassônico (trigger)
    gpio_put(TRIG_PIN, 1); // Define o pino TRIG como nível alto
    sleep_us(10);          // Mantém o nível alto por 10 microssegundos (pulso mínimo)
    gpio_put(TRIG_PIN, 0); // Define o pino TRIG como nível baixo (fim do pulso)

    // 2. Aguarda o sinal de "echo" do sensor (com timeout)
    uint32_t start = time_us_32(); // Marca o tempo de início da espera
    // Aguarda o pino ECHO ficar em nível alto (chegada do echo) ou timeout de 5ms
    while (!gpio_get(ECHO_PIN) && (time_us_32() - start) < 5000); 

    // 3. Mede a duração do pulso "echo" (com timeout)
    start = time_us_32(); // Reinicia o contador para medir o tempo do ECHO
    // Aguarda o pino ECHO ficar em nível baixo (fim do echo) ou timeout de 30ms
    while (gpio_get(ECHO_PIN) && (time_us_32() - start) < 30000);
    uint32_t duration = time_us_32() - start; // Calcula a duração do pulso echo

    // 4. Calcula a distância e verifica se está abaixo do limiar
    float distance = (duration * 0.0343) / 2; // Calcula a distância em cm.
                                             // 0.0343 é a velocidade do som em cm/us.
                                             // Dividimos por 2 porque o som vai e volta.

    if (distance < DISTANCE_THRESHOLD) { // Se a distância for menor que o limiar
        sleep_ms(10000); // Aguarda 10 segundos (tempo de "presença detectada") - REDUZIDO
        return true;      // Retorna verdadeiro (presença detectada)
    }
    return false;         // Retorna falso (presença não detectada)
}

char* virtual_keypad_with_msg(const char* msg, int cursor_col, int max_length, int exact_length) {
    const char keys[] = "0123456789ABCD*#"; // Teclado virtual: números, letras e símbolos
    int key_index = 0;                      // Índice da tecla selecionada no teclado virtual
    int pos = 0;                            // Posição atual no buffer de entrada

    lcd_clear(); // Limpa o display LCD
    lcd_set_cursor(0, 0); // Define o cursor para o início da primeira linha
    lcd_write_string(msg); // Exibe a mensagem (prompt) na primeira linha
    lcd_set_cursor(cursor_col, 1); // Define o cursor para a coluna especificada na segunda linha
    lcd_write_string("Input:");// Exibe o rótulo "Input:"

    key_index = 0; // Reinicia o índice da tecla

    while (pos < exact_length) { // Loop principal até preencher o número exato de caracteres
        lcd_set_cursor(6, 0); // Define o cursor para a posição da tecla virtual
        lcd_write_char(keys[key_index]); // Exibe a tecla virtual atual

        if (!gpio_get(UP_BUTTON_PIN)) { // Verifica se o botão UP foi pressionado
            key_index = (key_index + 1) % 16;// Avança para a próxima tecla (circular)
            sleep_ms(200); // Delay para evitar leituras repetidas do botão
        }
        if (!gpio_get(DOWN_BUTTON_PIN)) { // Verifica se o botão DOWN foi pressionado
            key_index = (key_index - 1 + 16) % 16; // Retrocede para a tecla anterior (circular)
            sleep_ms(200); // Delay para evitar leituras repetidas do botão
        }
        if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Verifica se o botão CONFIRM foi pressionado
            if (keys[key_index] == '#') { // Se a tecla '#' for pressionada
                if (pos == exact_length) { // Se o número exato de caracteres já foi atingido
                    keypad_input[pos] = '\0'; // Adiciona o caractere nulo ao final da string
                    break; // Sai do loop
                }
            } else { // Se outra tecla for pressionada
                keypad_input[pos] = keys[key_index]; // Adiciona a tecla selecionada ao buffer
                lcd_set_cursor(6 + pos, 1); // Define o cursor para a próxima posição no display
                lcd_write_char(keypad_input[pos]); // Escreve o caractere no display
                pos++;  // Avança para a próxima posição
                if (pos == exact_length) { // Se o número exato de caracteres já foi atingido
                    while (gpio_get(CONFIRM_BUTTON_PIN)) { // Aguarda o botão CONFIRM ser liberado
                        sleep_ms(100); // Delay para evitar leituras repetidas do botão
                    }
                    keypad_input[pos] = '\0'; // Adiciona o caractere nulo ao final da string
                    break; // Sai do loop
                }
                sleep_ms(200); // Delay para evitar leituras repetidas do botão
            }
        }
        sleep_ms(100);// Delay para evitar leituras repetidas dos botões
    }
    keypad_input[pos] = '\0'; // Adiciona o caractere nulo ao final da string
    return keypad_input; // Retorna o ponteiro para o buffer de entrada
}

// Verifica se a entrada fornecida corresponde ao código SUS armazenado.
bool verify_sus(const char* input) {
    return strcmp(input, SUS_CODE) == 0; // Compara as strings usando strcmp. Retorna 0 se forem iguais.
}

// Verifica se a entrada fornecida corresponde à senha armazenada.
bool verify_password(const char* input) {
    return strcmp(input, PASSWORD) == 0; // Compara as strings usando strcmp. Retorna 0 se forem iguais.
}

// Exibe o menu de seleção de medicamentos no LCD.
void display_menu(int* selected) {
    lcd_clear(); // Limpa o display LCD.
    lcd_set_cursor(0, 0); // Define o cursor para o início da primeira linha.
    // Exibe as opções de medicamentos, marcando a opção selecionada com ">".
    lcd_write_string(*selected == 0 ? ">1. Paracetamol" : " 1. Paracetamol");
    lcd_set_cursor(0, 1);// Define o cursor para o início da segunda linha.
    lcd_write_string(*selected == 1 ? ">2. Ibuprofeno" : " 2. Ibuprofeno");
}

// Função para o usuário selecionar um medicamento no menu.
int select_medicine() {
    int selected = 0; // Inicializa a opção selecionada como 0 (Paracetamol).
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_string("CONFIRM: ");
    lcd_write_char(gpio_get(CONFIRM_BUTTON_PIN) ? '1' : '0');
    sleep_ms(2000); // Aguarde 2 segundos para você visualizar

    while (true) { // Loop principal para navegação e seleção.

        if (!gpio_get(UP_BUTTON_PIN)) { // Se o botão UP for pressionado.
            if (selected > 0) selected--; // Decrementa a opção selecionada (limite superior).
            sleep_ms(200); // Delay para evitar leituras repetidas do botão.
        }
        if (!gpio_get(DOWN_BUTTON_PIN)) { // Se o botão DOWN for pressionado.
            if (selected < 1) selected++; // Incrementa a opção selecionada (limite inferior).
            sleep_ms(200); // Delay para evitar leituras repetidas do botão.
        }
        if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Se o botão CONFIRM for pressionado.
            sleep_ms(50); // Pequeno atraso para "debounce".
            if (!gpio_get(CONFIRM_BUTTON_PIN)) { //Confirma a seleção (segunda leitura para evitar falsos positivos).
                sleep_ms(200); // Delay para evitar leituras repetidas do botão.
                break; // Sai do loop de seleção.
            }
        }
        display_menu(&selected);
        sleep_ms(100); // Delay geral para evitar leituras repetidas dos botões.
    }
    return selected; // Retorna o índice do medicamento selecionado.
}

void release_medicine() {
    // Mover o servo motor para liberar o remédio (posição 2500)
    pwm_set_chan_level(pwm_gpio_to_slice_num(SERVO_PIN), PWM_CHAN_A, 2500);
    sleep_ms(1000); // Aguarda 1 segundo para o servo chegar à posição

    // Exibir mensagem "Retire sua dose"
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_string("Retire sua dose");

    // Aguardar 8 segundos
    sleep_ms(8000);

    // Retornar o servo motor à posição original (posição 1500)
    pwm_set_chan_level(pwm_gpio_to_slice_num(SERVO_PIN), PWM_CHAN_A, 1500);
    sleep_ms(1000); // Aguarda 1 segundo para o servo retornar
}

//Função economia para o display
void check_presence_and_sleep() {
    sleep_ms(5000); // Aguarda 5 segundos antes de verificar
    if (!detect_presence()) {
        display_message("Desligando...", false);
        lcd_clear();         // Limpa o display
        lcd_backlight(false); // Desliga o backlight
    }
}

int main() {
    stdio_init_all(); // Inicializa a entrada e saída padrão (para comunicação com o computador).
    init_peripherals(); // Inicializa os periféricos (LCD, servo, botões, sensor).
    /*destinado para realizar teste 
    void test_servo() {
        pwm_set_chan_level(pwm_gpio_to_slice_num(SERVO_PIN), PWM_CHAN_A, 1000);
        sleep_ms(2000);
        pwm_set_chan_level(pwm_gpio_to_slice_num(SERVO_PIN), PWM_CHAN_A, 2000);
        sleep_ms(2000);
    }
    */
    

    while (true) { // Loop principal do programa.
        if (detect_presence()) { // Verifica se há presença detectada pelo sensor.
            lcd_backlight(true); // Garante que o backlight esteja ligado ao detectar presença
            display_message("Bem Vindo. Seu remedio esta pronto!", true); // Exibe mensagem de boas-vindas.

            display_message("Digite SUS:", false); / Solicita o código SUS.
            char* sus_input = virtual_keypad_with_msg("Digite SUS", 0, 9, 9); // Exige exatamente 9 caracteres
            if (!verify_sus(sus_input)) { // Verifica se o código SUS é válido.
                display_message("Cartao Nao Aceito!", true); // Exibe mensagem de erro.
                sleep_ms(500); // Aguarda 500ms.
                continue; // Volta para o início do loop (aguarda nova presença).
            }

            display_message("Digite Senha:", false); // Solicita a senha.
            char* password_input = virtual_keypad_with_msg("Digite Senha", 0, 4, 4); // Exige exatamente 4 caracteres
            if (!verify_password(password_input)) { // Verifica se a senha é válida.
                display_message("Senha Incorreta!", true);// Exibe mensagem de erro.
                continue; // Volta para o início do loop (aguarda nova presença).
            }

            int selected = 0; // Variável para armazenar a opção de medicamento selecionada.
            display_menu(&selected); // Exibe o menu de medicamentos.
            int medicine = select_medicine(); // Permite o usuário selecionar o medicamento.
            release_medicine(); // Libera o medicamento selecionado.
            medicine_count--; // Decrementa a contagem de medicamentos disponíveis.

            check_presence_and_sleep();// Verifica se a pessoa ainda está presente e, se não, desliga o sistema.
        } else {// Se não houver presença detectada.
            sleep_ms(1000);// Aguarda 1 segundo e verifica novamente.
        }
    }
    return 0;// Fim do programa.
}
