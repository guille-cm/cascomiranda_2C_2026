/**
 * @file relevador_remoto.c
 * @brief Implementación del driver para el relevador remoto / pulsador de paciente (GPIO_8).
 * 
 * @details Provee la detección por flanco de bajada (transición de reposo en 3.3V a conducción en 0V),
 *          asegurando el filtrado temporal en conjunto con la red RC de hardware.
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del driver con detección por flanco |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include "relevador_remoto.h"
#include "driver/gpio.h"

/*==================[internal data declaration]==============================*/
static gpio_t s_rele_pin = RELEVADOR_DEFAULT_GPIO;
static bool s_rele_estado_previo = true;

/*==================[external functions definition]==========================*/
void RelevadorRemotoInit(gpio_t pin) {
    s_rele_pin = pin;

    // Configuración del registro GPIO del ESP32-C6:
    // - pin_bit_mask: Establece la máscara con el bit 8 en nivel alto (1ULL << GPIO_8).
    // - mode: Modo entrada de alta impedancia (GPIO_MODE_INPUT).
    // - pull_up_en: Habilita la resistencia pull-up interna complementaria (~45 kΩ) a 3.3V.
    // - pull_down_en: Deshabilita la red de pull-down interna.
    // - intr_type: Desactiva interrupciones para realizar muestreo determinístico por software.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_rele_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Muestreo inicial del estado del pin para inicializar la memoria de flanco
    // Si el contacto está abierto, GPIORead devuelve true (1 lógico = 3.3V)
    s_rele_estado_previo = GPIORead(s_rele_pin);
}

bool RelevadorRemotoEventoActivacion(void) {
    // Lectura del registro de entrada digital GPIO:
    // Retorna true (1) si el pin está en 3.3V (reposo), o false (0) si fue llevado a masa (0V).
    bool lectura = GPIORead(s_rele_pin);

    // Detección de flanco descendente (High -> Low):
    // Se cumple cuando el estado instantáneo es 0V (!lectura es true) y en el ciclo anterior
    // se encontraba en nivel alto de 3.3V (s_rele_estado_previo == true).
    if (!lectura && s_rele_estado_previo) {
        s_rele_estado_previo = false; // Memoriza el estado presionado para no re-disparar en bucle
        return true;                  // Señaliza el evento a la tarea de FreeRTOS
    } else if (lectura && !s_rele_estado_previo) {
        // Flanco ascendente (Low -> High): El contacto se abrió y retornó a 3.3V
        s_rele_estado_previo = true;  // Restaura la memoria de flanco para permitir una nueva pulsación
    }
    return false; // Sin evento de transición activa
}

bool RelevadorRemotoEstadoActual(void) {
    // Retorna true únicamente cuando el pin se encuentra en nivel bajo sostenido (0V)
    return !GPIORead(s_rele_pin);
}

/*==================[end of file]============================================*/