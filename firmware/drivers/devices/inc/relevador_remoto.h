#ifndef RELEVADOR_REMOTO_H
#define RELEVADOR_REMOTO_H
/**
 * @file relevador_remoto.h
 * @brief Driver de la capa de dispositivos para el accionamiento por relevador a control remoto
 *        y pulsador de llamada de paciente sobre el pin de strapping GPIO_8 del ESP32-C6.
 * 
 * @details Este módulo gestiona la lectura con acondicionamiento físico y digital de un contacto
 *          seco Normalmente Abierto (NO), conmutado tanto por un pulsador cableado como por un
 *          módulo receptor inalámbrico de radiofrecuencia de 433 MHz (modelo RX1C).
 * 
 * @section rff_strat Estrategia de Funcionamiento y Acondicionamiento de Hardware
 * 1. <b>Etapa de Potencia y Elevación de Tensión:</b>
 *    El módulo receptor inalámbrico RX1C incorpora internamente un regulador lineal 78L05 para
 *    su circuito lógico y decodificador EV1527, pero la bobina de su relé electromecánico exige
 *    una tensión nominal de 12V DC para vencer la fuerza mecánica de su armadura. Dado que todo
 *    el sistema se alimenta a través de la interfaz USB de 5V de la computadora o cargador, se
 *    emplea un módulo convertidor elevador conmutado (Step-Up) MT3608 ajustado exactamente a 12.0V.
 *    Para evitar que el rizado de alta frecuencia (1.2 MHz) del convertidor conmutado interfiera
 *    con los buses digitales de 5V (I2C del display LCD y Single-Wire del DHT22), la entrada del
 *    MT3608 se desacopla con un capacitor electrolítico de 100 µF en paralelo con un cerámico de
 *    100 nF (104). Asimismo, la salida de 12V cuenta con un capacitor electrolítico de 220 µF a 35V,
 *    el cual entrega el pico transitorio de corriente de la bobina (~40 mA) al momento de conmutar.
 * 
 * 2. <b>Aislamiento Galvánico y Red de Filtrado Antirrebote en GPIO_8:</b>
 *    Los bornes de salida del relé (COM y NO) constituyen contactos secos mecánicamente aislados
 *    de la alimentación de 12V. Para ingresar al microcontrolador de forma inmune a transitorios y
 *    ruido electromagnético, se adoptó la topología de acondicionamiento de las teclas del ESP-EDU:
 *    - Un resistor de pull-up externo de 10 kΩ polariza un nodo central a 3.3V.
 *    - El terminal NO del relé se conecta a dicho nodo y el terminal COM se vincula a masa (GND).
 *    - Un capacitor cerámico multicapa 104 (100 nF) se conecta desde el nodo hacia masa, formando
 *      un filtro pasabajos de constante de tiempo RC = 1 ms que amortigua los rebotes mecánicos.
 *    - Desde el nodo central, una resistencia de protección de 1 kΩ ingresa al pin GPIO_8 del microcontrolador.
 * 
 * 3. <b>Tratamiento del Pin de Strapping GPIO_8:</b>
 *    En el ESP32-C6, GPIO_8 gobierna el modo de arranque durante el reset (debe estar en nivel ALTO
 *    durante la descarga del bootloader si GPIO_9 se pone en bajo). Al utilizar el contacto
 *    Normalmente Abierto (NO), en reposo el circuito permanece abierto y la resistencia de pull-up
 *    de 10 kΩ garantiza un nivel lógico alto de 3.3V permanente, permitiendo flashear el programa
 *    y arrancar el microcontrolador sin bloqueos.
 * 
 * 4. <b>Lógica de Doble Propósito en el Sistema Operativo (FreeRTOS):</b>
 *    Desde la tarea periódica TaskPotenciometro_LED (ejecutada cada 20 ms), se evalúa la función
 *    RelevadorRemotoEventoActivacion(). Si la cola FIFO de alarmas se encuentra vacía, llevar GPIO_8
 *    a masa indica una llamada prioritaria del paciente (activando la bandera de atención, encendiendo
 *    el LED 3 en modo continuo y disparando la melodía RTTTL Drama). Si la cola FIFO contiene al menos
 *    una alarma, la misma pulsación desencola la alerta más antigua y apaga los avisos sonoros.
 * 
 * @section hardConn Hardware Connection
 * | Componente / Terminal     | Pin ESP32-C6 / Circuito | Descripción de la Conexión Física |
 * | :------------------------ | :---------------------- | :--------------------------------- |
 * | Entrada USB +5V           | VIN+ (MT3608)           | Filtrada con 100 µF + 100 nF a masa |
 * | Salida +12.0V MT3608      | +V (Módulo RX1C)        | Filtrada con 220 µF a 35V a masa   |
 * | Masa común                | GND (VOUT- / VIN-)      | Plano de masa unificado (0V)       |
 * | Contacto Común (COM)      | GND                     | Conexión a masa del contacto seco  |
 * | Contacto Norm. Abierto(NO)| Nodo de disparo         | A resistencia pull-up 10 kΩ a 3.3V |
 * | Filtro pasabajos (104)    | Nodo a GND              | Capacitor cerámico de 100 nF       |
 * | Resistor de protección    | Nodo a GPIO_8           | Resistencia de 1 kΩ en serie       |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del driver modular para relevador a control remoto en GPIO_8 |
 * 
 * @version 1.3
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
#define RELEVADOR_DEFAULT_GPIO      GPIO_8  /*!< Pin GPIO asignado al nodo de disparo del relevador */

/*==================[external function declarations]=========================*/
/**
 * @fn void RelevadorRemotoInit(gpio_t pin)
 * @brief Configura el pin GPIO_8 como entrada digital con resistencia pull-up activa.
 * @param[in] pin Pin GPIO asignado al contacto del relevador (por defecto GPIO_8).
 */
void RelevadorRemotoInit(gpio_t pin);

/**
 * @fn bool RelevadorRemotoEventoActivacion(void)
 * @brief Detecta el flanco descendente (transición de 3.3V a 0V) originado al cerrar el contacto seco.
 * @return true una única vez por cada accionamiento del control remoto o pulsador; false en reposo.
 */
bool RelevadorRemotoEventoActivacion(void);

/**
 * @fn bool RelevadorRemotoEstadoActual(void)
 * @brief Consulta el nivel lógico instantáneo presente en el pin.
 * @return true si el contacto está cerrado a GND (0V); false si está abierto en reposo (3.3V).
 */
bool RelevadorRemotoEstadoActual(void);

#endif /* RELEVADOR_REMOTO_H */
/*==================[end of file]============================================*/