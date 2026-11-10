#ifndef AVISO_SONORO_H
#define AVISO_SONORO_H
/**
 * @file aviso_sonoro.h
 * @brief Driver de la capa de dispositivos para la generación de alertas acústicas PWM,
 *        reproducción de melodías RTTTL y síntesis de voz digital LPCM en Clase D sobre GPIO_23 del ESP32-C6.
 * 
 * @details Desacopla el motor de hardware (periférico LEDC + temporizador de alta resolución SYSTIMER)
 *          de los datos de audio: los vectores de locución hablada se almacenan en archivos de cabecera
 *          independientes dentro de \c devices/inc/sonidos_lpcm/ (por ejemplo, \c ayuda_en_camino.h) en la
 *          sección \c .rodata de la memoria Flash, mientras que las partituras monofónicas provienen de
 *          \c buzzer_melodies.h, manteniendo libre la memoria RAM del sistema. @n
 * 
 * @section audio_lpcm_theory Fundamentos Teóricos de Audio Digital LPCM, Cadena de Procesamiento de Voz y Modulación PWM Clase D
 * 
 * ### <b>1. Naturaleza Matemática de una Señal Acústica Digitalizada (LPCM)</b>
 * Una onda sonora continua en el tiempo <b>s(t)</b> consiste en variaciones de presión acústica que, al ser
 * captadas por el transductor de un micrófono, se convierten en una señal eléctrica analógica continua <b>v(t)</b>.
 * El formato <b>LPCM (<i>Linear Pulse Code Modulation</i> o Modulación por Impulsos Codificados Lineal)</b> representa
 * dicha función continua mediante una secuencia discreta de números enteros obtenidos a través de dos procesos
 * fundamentales regidos por el Teorema de Muestreo de Nyquist-Shannon:
 * - <b>Discretización Temporal (Muestreo a f<sub>s</sub> = 8000 Hz):</b> La función continua <b>v(t)</b> se evalúa en
 *   instantes equidistantes separados por un período de muestreo constante <b>T<sub>s</sub> = 1 / f<sub>s</sub> = 1 / 8000 Hz = 125 µs</b>.
 *   De este modo, a cada índice entero del arreglo <b>n ∈ {0, 1, 2, ..., N-1}</b> le corresponde de forma biunívoca
 *   un instante físico de tiempo <b>t<sub>n</sub> = n · T<sub>s</sub> = n · 125 µs</b>.
 * - <b>Cuantización Lineal de Amplitud (8 bits con signo, \c int8_t):</b> En cada instante <b>t<sub>n</sub></b>, el valor instantáneo
 *   de amplitud <b>v(t<sub>n</sub>)</b> se aproxima a uno de los <b>2<sup>8</sup> = 256</b> niveles enteros uniformemente espaciados dentro del
 *   intervalo <b>[-128, +127]</b>. El valor <b>0</b> representa el punto de reposo mecánico del cono del parlante (silencio);
 *   los valores positivos (<b>+1 a +127</b>) representan semiciclos de compresión acústica (desplazamiento del cono
 *   hacia adelante) y los valores negativos (<b>-1 a -128</b>) representan semiciclos de rarefacción (retracción
 *   del cono), reconstruyendo fielmente las frecuencias vocales humanas hasta la frecuencia de Nyquist
 *   <b>f<sub>Nyquist</sub> = f<sub>s</sub> / 2 = 4000 Hz</b>.
 * 
 * ### <b>2. Cadena de Procesamiento desde la Grabación en Celular (.m4a) hasta el Arreglo en Lenguaje C (.h)</b>
 * Para generar la locución clínica <i>"Ayuda en camino"</i> alojada en \c sonidos_lpcm/ayuda_en_camino.h, se aplicó
 * la siguiente secuencia de ingeniería de audio digital utilizando el software <b>Audacity</b>:
 * 1. <b>Captura primaria (.m4a):</b> Grabación de la voz mediante el micrófono de un teléfono celular en formato
 *    comprimido AAC dentro de un contenedor \c .m4a (originalmente estéreo/mono a <b>44.1 kHz</b> o <b>48 kHz</b>).
 * 2. <b>Decodificación y Conversión a Monoaural (Mono):</b> Importación del archivo en Audacity, decodificando las
 *    tramas psicoacústicas AAC a muestras lineales en punto flotante de 32 bits y unificando la pista a un único
 *    canal monofónico.
 * 3. <b>Remuestreo (<i>Resampling</i>) a 8000 Hz:</b> Conversión de la frecuencia de proyecto a <b>f<sub>s</sub> = 8000 muestras/s</b>
 *    con filtro pasabajos <i>anti-aliasing</i> previo en <b>4 kHz</b>, reduciendo en casi 6 veces la cantidad de bytes
 *    por segundo sin perder inteligibilidad telefónica en las consonantes y formantes de la voz.
 * 4. <b>Filtrado Pasaaltos / Pasabanda:</b> Aplicación de un filtro pasaaltos (<i>High-Pass Filter</i>, frecuencia de corte
 *    <b>f<sub>c</sub> ≈ 100 a 150 Hz</b>) para suprimir componentes subsónicas, golpes de aire oclusivos (<i>plosives</i>)
 *    y frecuencias graves que un parlante compacto no puede reproducir y que solo saturarían el rango dinámico.
 * 5. <b>Compresión de Rango Dinámico y Normalización:</b> Aplicación de un compresor dinámico para elevar las sílabas
 *    débiles respecto de los picos vocales (reduciendo el factor de cresta y mejorando la relación señal a ruido de
 *    cuantización en 8 bits), seguido de una normalización de pico a <b>-0.5 dBFS</b> para aprovechar la totalidad
 *    de la excursión digital entre <b>-128</b> y <b>+127</b>.
 * 6. <b>Exportación a LPCM Crudo (<i>Raw Headerless PCM</i>):</b> Exportación desde Audacity como datos de audio sin
 *    encabezado RIFF/WAV (<i>Other uncompressed files -> RAW header-less -> Signed 8-bit PCM</i>), obteniendo un flujo
 *    binario puro donde cada byte consecutivo representa exactamente una muestra <b>x[n] ∈ [-128, +127]</b> cada <b>125 µs</b>.
 * 7. <b>Binarización a Vector Constante de C:</b> Conversión del flujo binario a una matriz estática de lenguaje C
 *    (\c static \c const \c int8_t \c ayuda_en_camino_pcm[]) donde el índice del arreglo <b>n</b> modela el eje temporal
 *    (<b>t<sub>n</sub> = n · 125 µs</b>) y el contenido almacenado \c ayuda_en_camino_pcm[n] modela la ordenada de
 *    amplitud instantánea <b>x(t<sub>n</sub>)</b>.
 * 
 * ### <b>3. Reconstrucción Física mediante Modulación por Ancho de Pulso (PWM Clase D a 32 kHz)</b>
 * Como el pin \c GPIO_23 es una salida digital que conmuta entre <b>0 V</b> y <b>3.3 V</b>, la conversión
 * digital-analógica se realiza mediante <b>Modulación de Ancho de Pulso (PWM) en Clase D</b>:
 * - <b>Portadora Ultrasónica (f<sub>carrier</sub> = 32000 Hz):</b> El temporizador \c LEDC_TIMER_0 se configura con
 *   una resolución de <b>8 bits</b> (<b>2<sup>8</sup> = 256</b> escalones de ciclo de trabajo, de <b>0</b> a <b>255</b>) a una frecuencia de
 *   portadora de <b>32 kHz</b> (4 ciclos completos de portadora por cada muestra de audio de <b>125 µs</b>).
 *   Al ser <b>32 kHz</b> superior al límite audible humano (<b>20 kHz</b>), la portadora resulta inaudible.
 * - <b>Mapeo de Amplitud a Ciclo de Trabajo (<i>Duty Cycle</i>):</b> Cada <b>125 µs</b>, la rutina de interrupción del
 *   temporizador de hardware \c esp_timer (\c pcm_sample_timer_callback) lee la muestra con signo \c s_pcm_ptr[s_pcm_index],
 *   aplica una ganancia digital de <b>×2</b> con saturación simétrica en <b>[-128, +127]</b> y le suma el nivel de
 *   polarización de punto medio <b>\c AVISO_PCM_NEUTRAL_DUTY (\c 128)</b>, proyectando el intervalo bipolar <b>[-128, +127]</b>
 *   sobre el intervalo unipolar de ciclo de trabajo <b>[0, 255]</b>: @n
 *   <b>Duty<sub>8bit</sub>[n] = clip(2 · x[n], -128, +127) + 128</b>
 * - <b>Filtrado Mecánico e Inductivo en el Parlante:</b> La inductancia propia de la bobina móvil del parlante de <b>4 Ω</b>
 *   y la inercia mecánica de su cono y suspensión actúan como un filtro pasabajos acústico natural que rechaza la
 *   portadora de <b>32 kHz</b> e integra el valor medio de cada pulso PWM, haciendo que el desplazamiento físico de
 *   la membrana siga proporcionalmente la envolvente de voz de <b>8 kHz</b>.
 * 
 * ### <b>4. Motor de Melodías RTTTL e Integración No Bloqueante en FreeRTOS</b>
 * Adicionalmente, el módulo incorpora un intérprete asíncrono del formato <b>RTTTL (<i>Ring Tone Text Transfer Language</i>)</b>
 * capaz de ejecutar en segundo plano las melodías almacenadas en \c buzzer_melodies.h (\c songMarioHurryUp_version_guille_cm
 * para alertas de la cola FIFO, \c songDrama_version_guille_cm para el llamado de paciente desde \c GPIO_8 y
 * \c songPacman_version_guille_cm para el aviso web). En lugar de utilizar lazos de demora bloqueantes, el
 * intérprete calcula la frecuencia en Hertz de cada nota musical y su duración en milisegundos, reprogramando un
 * temporizador de disparo único (\c esp_timer_start_once) que avanza puntero a puntero sobre la cadena RTTTL.
 * 
 * Gracias a esta arquitectura basada íntegramente en temporizadores de hardware (\c LEDC y \c SYSTIMER), las funciones
 * públicas (\c AvisoSonoroReproducirVozAyuda(), \c AvisoSonoroReproducirRtttl(), \c AvisoSonoroDetenerRtttl()) retornan
 * en pocos microsegundos a las tareas de FreeRTOS (\c TaskPotenciometro_LED y \c TaskDisplayLCD en Prioridad 2),
 * sin interferir jamás con el muestreo de <b>500 Hz</b> de \c TaskGenerarECG (Prioridad 5) y \c TaskLeerECG_UART (Prioridad 4).
 * 
 * @section audio_hw_specs Especificaciones Eléctricas, Estudio en Osciloscopio de la Línea de +5V y Etapa Push-Pull Definitiva
 * - <b>Diagnóstico Experimental con Osciloscopio sobre el Riel de +5V:</b>
 *   Durante las pruebas de carga con el parlante dinámico de baja impedancia (<b>4 Ω</b>), se verificó con el
 *   osciloscopio (configurado en acoplamiento DC, <b>1.00 V/div</b> y disparo por flanco negativo en <b>4.67 V</b>
 *   y <b>4.71 V</b>) que la conmutación directa de picos de corriente de <b>≈ 250 a 300 mA</b> hacia
 *   la bobina móvil producía caídas transitorias en la línea de <b>+5V</b> USB que corrompían las tramas del
 *   expansor I2C \c PCF8574 del display LCD.
 * - <b>Solución de Hardware Definitiva (Etapa Seguidora de Emisor Push-Pull Clase AB/B con Tanque de Desacople):</b>
 *   1. <b>Excitación de Base:</b> La salida \c GPIO_23 (lógica de <b>3.3 V</b>) ataca a través de un resistor limitador
 *      de corriente de base <b>R<sub>B</sub> = 220 Ω</b> las bases unidas de un par complementario bipolar: un transistor NPN
 *      (<b>\c C1815</b>) en la rama superior y un transistor PNP (<b>\c A1015</b>) en la rama inferior.
 *   2. <b>Aislamiento de Fuente y Capacitor Tanque (1000 µF):</b> La línea general de <b>+5V</b> alimenta el
 *      colector del transistor NPN (\c C1815) a través de un elemento de desacoplo (diodo rectificador \c 1N4007 / resistor
 *      de aislamiento), disponiendo inmediatamente entre el colector del \c C1815 y masa (<b>\c GND</b>) un capacitor
 *      electrolítico tanque de <b>C<sub>tanque</sub> = 1000 µF</b> (en paralelo con <b>100 nF</b> cerámico). De esta
 *      forma, los picos de corriente de audio son suministrados localmente por el capacitor de <b>1000 µF</b>,
 *      manteniendo el bus de <b>+5V</b> en <b>4.9 V</b> estables con muescas inferiores a <b>150 mV</b> (confirmado
 *      en las capturas de osciloscopio en modo Normal y Single). El colector del transistor PNP (\c A1015) se conecta
 *      directamente a <b>\c GND</b>.
 *   3. <b>Acople de Salida en Alterna y Limitación Serie:</b> Los emisores de ambos transistores (\c C1815 y \c A1015) se
 *      unen entre sí y se conectan al terminal positivo (+) de un capacitor electrolítico de bloqueo de continua de
 *      <b>C<sub>acople</sub> = 220 µF</b> (que impide la circulación de corriente continua por la bobina del parlante
 *      y forma un filtro pasaaltos acústico con la carga). El terminal negativo (-) del capacitor de <b>220 µF</b>
 *      se conecta en serie con un resistor limitador de potencia de <b>R<sub>lim</sub> = 10 Ω</b> hacia el borne positivo
 *      del parlante dinámico de <b>4 Ω</b>, cerrando el retorno del parlante a <b>\c GND común</b>.
 * 
 * @section audio_media Video Experimental y Esquema del Amplificador Push-Pull
 * - <b>[Etapa sonido para avisos LPCM y PWM](https://youtu.be/Lmkvz8ZtWx4)</b> @n
 *   Mediante la modulación LPCM pueden enviarse sonidos elaborados como mensajes hacia el monitor unidad paciente. @n
 * 
 * @see [Esquema de la Etapa Amplificadora Push-Pull de Audio](../amplificador.jpg)
 * 
 * @section hardConn Hardware Connection
 * | Dispositivo / Etapa Push-Pull | Pin ESP32-C6 | Conexión Hardware / Circuito Externo |
 * | :---------------------------- | :----------- | :----------------------------------- |
 * | Entrada de Señal Audio / PWM  | GPIO_23      | A resistor de base 220 Ω → Bases unidas de C1815 (NPN) y A1015 (PNP) |
 * | Colector NPN (C1815)          | 5V           | Alimentado desde +5V vía diodo 1N4007 con capacitor tanque de 1000 µF a GND |
 * | Colector PNP (A1015) y Parl.  | GND          | Masa común del sistema (0V)          |
 * | Salida Emisores Comunes       | -            | A (+) de capacitor electrolítico 220 µF → 10 Ω → Parlante 4 Ω → GND |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del driver de tonos PWM y motor de reproducción LPCM en GPIO_23 |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include "gpio_mcu.h"

/*==================[macros and definitions]=================================*/
#define AVISO_SONORO_DEFAULT_GPIO   GPIO_23 /*!< Pin GPIO asignado a la salida de audio PWM / LPCM */
#define AVISO_FREQ_ALARMA_FIFO_HZ   2500    /*!< Frecuencia aguda (2.5 kHz) para alarma de cola FIFO */
#define AVISO_FREQ_ADVERTENCIA_HZ   1800    /*!< Frecuencia media (1.8 kHz) para advertencia de 3 s */

/**
 * @brief Macro auxiliar para reproducir cualquier arreglo estático de audio LPCM
 *        calculando automáticamente su cantidad de muestras en bytes mediante \c sizeof().
 */
#define AVISO_SONORO_PLAY(arreglo_pcm) \
    AvisoSonoroReproducirPcm((arreglo_pcm), (uint32_t)sizeof(arreglo_pcm))

/*==================[external function declarations]=========================*/
/**
 * @fn void AvisoSonoroInit(gpio_t pin)
 * @brief Inicializa el periférico LEDC PWM en el pin indicado con reloj de cristal de 40 MHz
 *        (\c LEDC_USE_XTAL_CLK) y crea los temporizadores de alta resolución (\c esp_timer) para
 *        el muestreo periódico de voz LPCM a 8000 Hz (125 us) y para la secuenciación de notas RTTTL.
 * @param[in] pin Pin GPIO asignado a la etapa de potencia Push-Pull (por defecto \c GPIO_23).
 */
void AvisoSonoroInit(gpio_t pin);

/**
 * @fn void AvisoSonoroEncenderTono(uint32_t freq_hz)
 * @brief Emite una onda cuadrada continua (ciclo de trabajo del 50%, valor 128) a la frecuencia indicada,
 *        siempre que no se encuentre activa una locución LPCM ni una melodía RTTTL.
 * @param[in] freq_hz Frecuencia del tono audible en Hertz.
 */
void AvisoSonoroEncenderTono(uint32_t freq_hz);

/**
 * @fn void AvisoSonoroApagar(void)
 * @brief Silencia el generador de tonos llevando el ciclo de trabajo del canal LEDC a 0% (0V en reposo),
 *        respetando la finalización de locuciones LPCM o melodías RTTTL si estuvieran en curso.
 */
void AvisoSonoroApagar(void);

/**
 * @fn void AvisoSonoroReproducirPcm(const int8_t *pcm_data, uint32_t num_samples)
 * @brief Reproduce de forma asíncrona (no bloqueante) un vector de muestras de audio LPCM de 8 bits
 *        con signo a una tasa de 8000 Hz (1 muestra cada 125 us) modulando una portadora PWM de 32 kHz.
 * @param[in] pcm_data Puntero al arreglo constante de muestras (\c int8_t) alojado en memoria Flash (\c .rodata).
 * @param[in] num_samples Cantidad total de muestras (bytes) que componen el vector de audio.
 */
void AvisoSonoroReproducirPcm(const int8_t *pcm_data, uint32_t num_samples);

/**
 * @fn void AvisoSonoroReproducirVozAyuda(void)
 * @brief Inicia la reproducción asíncrona de la locución hablada \c ayuda_en_camino_pcm ("Ayuda en camino")
 *        alojada en \c "sonidos_lpcm/ayuda_en_camino.h".
 */
void AvisoSonoroReproducirVozAyuda(void);

/**
 * @fn bool AvisoSonoroReproduciendoVoz(void)
 * @brief Consulta si el motor de síntesis LPCM se encuentra reproduciendo una locución en este instante.
 * @return \c true mientras el temporizador de 125 us continúa barriendo el arreglo de voz; \c false al concluir.
 */
bool AvisoSonoroReproduciendoVoz(void);

/**
 * @fn void AvisoSonoroReproducirRtttl(const char *rtttl_melody)
 * @brief Analiza el encabezado de una cadena musical en formato RTTTL (\c d, \c o, \c b) e inicia su
 *        reproducción nota por nota en segundo plano mediante el temporizador \c s_rtttl_timer_handle.
 * @param[in] rtttl_melody Puntero a la cadena de texto con la partitura RTTTL (ej. \c songMarioHurryUp_version_guille_cm).
 */
void AvisoSonoroReproducirRtttl(const char *rtttl_melody);

/**
 * @fn void AvisoSonoroDetenerRtttl(void)
 * @brief Detiene de inmediato el temporizador de notas RTTTL y lleva la salida PWM a 0V (silencio).
 */
void AvisoSonoroDetenerRtttl(void);

/**
 * @fn bool AvisoSonoroReproduciendoRtttl(void)
 * @brief Consulta si el intérprete RTTTL se encuentra ejecutando una melodía actualmente.
 * @return \c true si hay una melodía en reproducción; \c false si se encuentra detenido.
 */
bool AvisoSonoroReproduciendoRtttl(void);

#endif /* AVISO_SONORO_H */
/*==================[end of file]============================================*/