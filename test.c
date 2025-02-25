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
#define DISTANCE_THRESHOLD 20 // cm
#define SUS_CODE "233322332"
#define PASSWORD "2233"

// Definições do LCD I2C
#define I2C_PORT i2c0
#define LCD_ADDRESS 0x27
#define LCD_DELAY_US 2000

// Variáveis globais
int medicine_count = 10;
char keypad_input[16];
uint8_t backlight_state = 0x08; // Ligado por padrão

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
void lcd_send_byte(uint8_t data, uint8_t rs) {
    uint8_t buf[4];
    uint8_t high_nibble = (data & 0xF0) | rs | backlight_state;
    uint8_t low_nibble = ((data << 4) & 0xF0) | rs | backlight_state;

    buf[0] = high_nibble | 0x04;
    buf[1] = high_nibble;
    buf[2] = low_nibble | 0x04;
    buf[3] = low_nibble;

    i2c_write_blocking(I2C_PORT, LCD_ADDRESS, buf, 4, false);
    sleep_us(LCD_DELAY_US);
}

void lcd_init() {
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(50);
    lcd_send_byte(0x03 << 4, 0); sleep_ms(5);
    lcd_send_byte(0x03 << 4, 0); sleep_us(150);
    lcd_send_byte(0x03 << 4, 0);
    lcd_send_byte(0x02 << 4, 0);

    lcd_send_byte(0x28, 0);
    lcd_send_byte(0x0C, 0);
    lcd_send_byte(0x01, 0);
    lcd_send_byte(0x06, 0);
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
void lcd_backlight(bool on) {
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
    lcd_init();
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(SERVO_PIN);
    pwm_set_wrap(slice_num, 20000);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 1500);
    pwm_set_enabled(slice_num, true);

    // Configuração dos botões com pull-up interno
    gpio_init(UP_BUTTON_PIN);
    gpio_set_dir(UP_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(UP_BUTTON_PIN);

    gpio_init(DOWN_BUTTON_PIN);
    gpio_set_dir(DOWN_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(DOWN_BUTTON_PIN);

    gpio_init(CONFIRM_BUTTON_PIN);
    gpio_set_dir(CONFIRM_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(CONFIRM_BUTTON_PIN);

    // Configuração do sensor HC-SR04
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
}

bool detect_presence() {
    gpio_put(TRIG_PIN, 1);
    sleep_us(10);
    gpio_put(TRIG_PIN, 0);

    uint32_t start = time_us_32();
    while (!gpio_get(ECHO_PIN) && (time_us_32() - start) < 5000);
    start = time_us_32();
    while (gpio_get(ECHO_PIN) && (time_us_32() - start) < 30000);
    uint32_t duration = time_us_32() - start;

    float distance = (duration * 0.0343) / 2;
    if (distance < DISTANCE_THRESHOLD) {
        sleep_ms(10000); // Reduzido de 20s para 10s
        return true;
    }
    return false;
}

char* virtual_keypad_with_msg(const char* msg, int cursor_col, int max_length, int exact_length) {
    const char keys[] = "0123456789ABCD*#";
    int key_index = 0;
    int pos = 0;

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_string(msg);
    lcd_set_cursor(cursor_col, 1);
    lcd_write_string("Input:");

    key_index = 0;

    while (pos < exact_length) {
        lcd_set_cursor(6, 0);
        lcd_write_char(keys[key_index]);

        if (!gpio_get(UP_BUTTON_PIN)) { // Botão UP
            key_index = (key_index + 1) % 16;
            sleep_ms(200);
        }
        if (!gpio_get(DOWN_BUTTON_PIN)) { // Botão DOWN
            key_index = (key_index - 1 + 16) % 16;
            sleep_ms(200);
        }
        if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Botão CONFIRM
            if (keys[key_index] == '#') {
                if (pos == exact_length) {
                    keypad_input[pos] = '\0';
                    break;
                }
            } else {
                keypad_input[pos] = keys[key_index];
                lcd_set_cursor(6 + pos, 1);
                lcd_write_char(keypad_input[pos]);
                pos++;
                if (pos == exact_length) {
                    while (gpio_get(CONFIRM_BUTTON_PIN)) {
                        sleep_ms(100);
                    }
                    keypad_input[pos] = '\0';
                    break;
                }
                sleep_ms(200);
            }
        }
        sleep_ms(100);
    }
    keypad_input[pos] = '\0';
    return keypad_input;
}


bool verify_sus(const char* input) {
    return strcmp(input, SUS_CODE) == 0;
}

bool verify_password(const char* input) {
    return strcmp(input, PASSWORD) == 0;
}

void display_menu(int* selected) {
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_string(*selected == 0 ? ">1. Paracetamol" : " 1. Paracetamol");
    lcd_set_cursor(0, 1);
    lcd_write_string(*selected == 1 ? ">2. Ibuprofeno" : " 2. Ibuprofeno");
}

int select_medicine() {
    int selected = 0;
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_write_string("CONFIRM: ");
    lcd_write_char(gpio_get(CONFIRM_BUTTON_PIN) ? '1' : '0');
    sleep_ms(2000); // Aguarde 2 segundos para você visualizar
    while (true) {

        if (!gpio_get(UP_BUTTON_PIN)) { // Botão UP
            if (selected > 0) selected--;
            sleep_ms(200);
        }
        if (!gpio_get(DOWN_BUTTON_PIN)) { // Botão DOWN
            if (selected < 1) selected++;
            sleep_ms(200);
        }
        if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Botão CONFIRM
            sleep_ms(50); // Pequeno atraso
            if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Confirma novamente
                sleep_ms(200);
                break;
            }
        }
        display_menu(&selected);
        sleep_ms(100);
    }
    return selected;
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
    stdio_init_all();
    init_peripherals();
    void test_servo() {
        pwm_set_chan_level(pwm_gpio_to_slice_num(SERVO_PIN), PWM_CHAN_A, 1000);
        sleep_ms(2000);
        pwm_set_chan_level(pwm_gpio_to_slice_num(SERVO_PIN), PWM_CHAN_A, 2000);
        sleep_ms(2000);
    }

    while (true) {
        if (detect_presence()) {
            lcd_backlight(true); // Garante que o backlight esteja ligado ao detectar presença
            display_message("Bem Vindo. Seu remedio esta pronto!", true);

            display_message("Digite SUS:", false);
            char* sus_input = virtual_keypad_with_msg("Digite SUS", 0, 9, 9); // Exige exatamente 9 caracteres
            if (!verify_sus(sus_input)) {
                display_message("Cartao Nao Aceito!", true);
                sleep_ms(500);
                continue;
            }

            display_message("Digite Senha:", false);
            char* password_input = virtual_keypad_with_msg("Digite Senha", 0, 4, 4); // Exige exatamente 4 caracteres
            if (!verify_password(password_input)) {
                display_message("Senha Incorreta!", true);
                continue;
            }

            int selected = 0;
            display_menu(&selected);
            int medicine = select_medicine();
            release_medicine();
            medicine_count--;

            check_presence_and_sleep();
        } else {
            sleep_ms(1000);
        }
    }
    return 0;
}
