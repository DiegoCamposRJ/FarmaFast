//Código de Produção

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
    lcd_set_cursor(0, 1); // Define o cursor para o início da segunda linha.
    lcd_write_string(*selected == 1 ? ">2. Ibuprofeno" : " 2. Ibuprofeno");
}

// Função para o usuário selecionar um medicamento no menu.
int select_medicine() {
    int selected = 0; // Inicializa a opção selecionada como 0 (Paracetamol).

    // *** Esta parte foi modificada para melhorar a usabilidade ***
    while(gpio_get(CONFIRM_BUTTON_PIN) == 0){ //espera o botão ser solto antes de começar o menu
        sleep_ms(50);
    }
    display_menu(&selected); // Exibe o menu inicialmente

    while (true) { // Loop principal para navegação e seleção.

        if (!gpio_get(UP_BUTTON_PIN)) { // Se o botão UP for pressionado.
            if (selected > 0) selected--; // Decrementa a opção selecionada (limite superior).
            sleep_ms(200); // Delay para evitar leituras repetidas do botão.
            display_menu(&selected); // Atualiza o menu no display.
        }

        if (!gpio_get(DOWN_BUTTON_PIN)) { // Se o botão DOWN for pressionado.
            if (selected < 1) selected++; // Incrementa a opção selecionada (limite inferior).
            sleep_ms(200); // Delay para evitar leituras repetidas do botão.
            display_menu(&selected); // Atualiza o menu no display.
        }

        if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Se o botão CONFIRM for pressionado.
            sleep_ms(50); // Pequeno atraso para "debounce".
            if (!gpio_get(CONFIRM_BUTTON_PIN)) { // Confirma a seleção (segunda leitura para evitar falsos positivos).
                sleep_ms(200); // Delay para evitar leituras repetidas do botão.
                break; // Sai do loop de seleção.
            }
        }
        sleep_ms(50); // Delay geral para evitar leituras repetidas dos botões.
    }
    return selected; // Retorna o índice do medicamento selecionado.
}