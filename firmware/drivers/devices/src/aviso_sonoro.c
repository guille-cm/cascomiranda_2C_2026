/**
 * @file aviso_sonoro.c
 * @brief Implementación del motor de avisos acústicos (Tonos PWM, Melodías RTTTL y Síntesis de Voz LPCM a 8 kHz)
 *        sobre el periférico LEDC y el temporizador SYSTIMER del ESP32-C6.
 * 
 * @details Este archivo contiene la lógica de control de tiempo real del subsistema de audio:
 *          - Los archivos con los vectores de muestras se incluyen desde la carpeta
 *            \c "sonidos_lpcm/" (por ejemplo, \c "sonidos_lpcm/ayuda_en_camino.h").
 *          - Configura explícitamente la fuente de reloj \c LEDC_USE_XTAL_CLK (40 MHz) para
 *            conmutar sin errores de divisor fraccionario entre las frecuencias musicales RTTTL
 *            (octavas 3 a 7) y la portadora ultrasónica de 32 kHz empleada en la síntesis LPCM.
 *          - Establece una correspondencia temporal exacta entre el índice <b>n</b> del arreglo en C y
 *            el tiempo físico <b>t<sub>n</sub> = n · 125 µs</b>, entregando cada muestra de amplitud al
 *            modulador PWM a razón de 8000 actualizaciones por segundo para desplazar la membrana
 *            del parlante de <b>4 Ω</b> a través de la etapa Push-Pull (\c C1815 / \c A1015). @n
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Implementación inicial del motor LPCM no bloqueante a 8 kHz y tonos PWM |
 * 
 * @version 1.1
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include "aviso_sonoro.h"
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "driver/ledc.h"
#include "esp_timer.h"

/* Inclusión de los archivos de cabecera externos con los vectores constantes de voz LPCM en Flash (.rodata) */
#include "sonidos_lpcm/ayuda_en_camino.h"

/*==================[macros and definitions]=================================*/
#define AVISO_LEDC_TIMER            LEDC_TIMER_0        /*!< Temporizador LEDC 0 para generación PWM */
#define AVISO_LEDC_MODE             LEDC_LOW_SPEED_MODE /*!< Modo de velocidad estándar en ESP32-C6 */
#define AVISO_LEDC_CHANNEL          LEDC_CHANNEL_0      /*!< Canal 0 del periférico LEDC */

#define AVISO_PCM_CARRIER_HZ        32000               /*!< Portadora ultrasónica PWM Clase D (32 kHz) */
#define AVISO_PCM_SAMPLE_PERIOD_US  125                 /*!< Período de muestreo LPCM: 125 us = 8000 Hz */
#define AVISO_PCM_NEUTRAL_DUTY      128                 /*!< Punto medio de 8 bits (50% duty = 1.65V) */

/*==================[internal data declaration]==============================*/
static gpio_t s_audio_pin = AVISO_SONORO_DEFAULT_GPIO;  /*!<Almacena el número de pin GPIO configurado para la salida de audio (GPIO_23) */
static esp_timer_handle_t s_pcm_timer_handle = NULL;    /*!<Manejador del temporizador periódico de hardware (esp_timer) que dispara cada 125 us durante la voz LPCM */
static esp_timer_handle_t s_rtttl_timer_handle = NULL;  /*!<Manejador del temporizador de disparo único (esp_timer) que gobierna la duración de cada nota RTTTL */

static const int8_t * volatile s_pcm_ptr = NULL;    /*!< Puntero a la dirección base en memoria Flash del arreglo de muestras LPCM (int8_t) en reproducción */
static volatile uint32_t s_pcm_length = 0;          /*!< Cantidad total de muestras N del arreglo LPCM activo (duración total = N * 125 us)  */
static volatile uint32_t s_pcm_index = 0;           /*!< Índice temporal discreto actual 'n' (donde t_n = n * 125 us) que recorre el arreglo desde 0 hasta s_pcm_length - 1  */
static volatile bool s_reproduciendo_pcm = false;   /*!< Bandera volátil que indica si el flujo de voz LPCM se encuentra activo en segundo plano */
static volatile uint32_t s_freq_actual_hz = 0;      /*!< Memoria de la última frecuencia configurada en el periférico LEDC para evitar escrituras redundantes al registro */
static volatile bool s_tono_encendido = false;      /*!< Bandera volátil que indica si un tono continuo simple se encuentra encendido */

// Estado del parser RTTTL
static const char *s_rtttl_ptr = NULL;              /*!< Puntero de lectura que avanza carácter por carácter sobre la sección de notas de la cadena RTTTL activa */
static uint8_t s_default_dur = 4;                   /*!< Duración musical por defecto extraída del encabezado RTTTL (d=4: negra, d=8: corchea, d=16: semicorchea) */
static uint8_t s_default_oct = 6;                   /*!< Octava musical por defecto extraída del encabezado RTTTL (ej. o=5 u o=6) */
static uint16_t s_bpm = 63;                         /*!< Tempo en negras por minuto (Beats Per Minute, b=...) extraído del encabezado RTTTL */
static volatile bool s_reproduciendo_rtttl = false; /*!< Bandera volátil que indica si el secuenciador de melodías RTTTL está activo en segundo plano */

/*==================[internal functions declaration]=========================*/
static void aplicar_frecuencia_y_duty(uint32_t freq_hz, uint32_t duty);
static void pcm_sample_timer_callback(void *arg);
static void rtttl_timer_callback(void *arg);


/*==================[internal functions definition]==========================*/
/**
 * @brief Actualiza la frecuencia del temporizador LEDC (solo si varió respecto al estado previo)
 *        y aplica el nuevo ciclo de trabajo de 8 bits sobre el pin de salida GPIO_23.
 * @param[in] freq_hz Frecuencia deseada en Hertz (portadora de 32 kHz o frecuencia de nota musical).
 * @param[in] duty Ciclo de trabajo en escala de 8 bits (0 = 0% apagado, 128 = 50% onda cuadrada, 0..255 en LPCM).
 */
static void aplicar_frecuencia_y_duty(uint32_t freq_hz, uint32_t duty) {
    /*
     * 1. Verificación de cambio de frecuencia:
     *    Solo invoca ledc_set_freq() si la nueva frecuencia es válida (> 0) y distinta de la que
     *    ya se encuentra cargada en los divisores del Timer 0 (s_freq_actual_hz). Esto evita
     *    reiniciar innecesariamente la fase del contador de hardware cuando se mantiene la misma frecuencia.
     */
    if (freq_hz > 0 && freq_hz != s_freq_actual_hz) {
        ledc_set_freq(AVISO_LEDC_MODE, AVISO_LEDC_TIMER, freq_hz);
        s_freq_actual_hz = freq_hz;
    }
    /*
     * 2. Escritura y validación (latch) del ciclo de trabajo en el canal LEDC:
     *    - ledc_set_duty() carga el valor de 8 bits (0 a 255) en el registro sombra del canal 0.
     *    - ledc_update_duty() activa el bit de transferencia en el registro de configuración del canal
     *      para que el nuevo ancho de pulso se aplique físicamente en GPIO_23 en el siguiente ciclo PWM.
     */
    ledc_set_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL, duty);
    ledc_update_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL);
}

/**
 * @brief Callback del temporizador periódica de muestreo LPCM (ejecutada cada 125 us = 8000 Hz).
 * @details Materializa la reconstrucción temporal del sonido <b>x(t<sub>n</sub>)</b>: en cada interrupción de <b>125 µs</b>
 *          extrae la muestra <b>x[n]</b> en la posición \c s_pcm_index, la amplifica digitalmente, la desplaza
 *          al rango unipolar <b>[0, 255]</b> y actualiza el ancho del pulso de la portadora de <b>32 kHz</b> que
 *          excita la etapa Push-Pull y desplaza la membrana del parlante de <b>4 Ω</b>.
 * @param[in] arg Argumento genérico del temporizador (no utilizado).
 */
static void pcm_sample_timer_callback(void *arg) {
    /*
     * Verifica si la reproducción continúa activa y si el índice temporal discreto 's_pcm_index' (n)
     * aún no alcanzó el final del arreglo 's_pcm_length' (N).
     */
    if (s_reproduciendo_pcm && s_pcm_ptr != NULL && s_pcm_index < s_pcm_length) {
        
        /*
         * 1. Captura de la amplitud instantánea x[n] correspondiente al instante t_n = n * 125 us:
         *    Se lee el byte con signo int8_t (-128 a +127) desde la memoria Flash (.rodata) y se
         *    promueve a un entero con signo de 16 bits (int16_t) para poder multiplicar sin sufrir
         *    desbordamiento aritmético (overflow de 8 bits).
         */
        int16_t raw = (int16_t)s_pcm_ptr[s_pcm_index];
       
        /*
         * 2. Amplificación digital en banda base (Ganancia x2 = +6 dB) con limitación (clipping) simétrica:
         *    Se duplica la excursión de la muestra para maximizar la transferencia de energía acústica
         *    hacia el parlante. Si el producto supera el techo positivo (+127) o el piso negativo (-128)
         *    del cuantizador de 8 bits, se satura en el extremo correspondiente para evitar inversión de fase.
         */
        raw = raw * 2;
        if (raw > 127)  raw = 127;
        if (raw < -128) raw = -128;

        /*
         * 3. Conversión de escala bipolar con signo [-128, +127] a escala unipolar PWM [0, 255]:
         *    Al sumar la constante AVISO_PCM_NEUTRAL_DUTY (128):
         *    - Una amplitud negativa máxima (-128) se transforma en duty = 0   (0% de 32 kHz -> retracción máxima del cono).
         *    - El silencio acústico         (0)    se transforma en duty = 128 (50% de 32 kHz -> 1.65V medios bloqueados por el capacitor de 220 uF, cono en reposo).
         *    - Una amplitud positiva máxima (+127) se transforma en duty = 255 (100% de 32 kHz -> empuje máximo del cono).
         */
        uint32_t duty_8bit = (uint32_t)(raw + AVISO_PCM_NEUTRAL_DUTY);
        
        /*
         * 4. Entrega del valor modulado a la salida física GPIO_23 e incremento del índice temporal:
         *    Durante los próximos 125 us, la portadora de 32 kHz ejecutará 4 períodos completos con este
         *    ancho de pulso 'duty_8bit', inyectando corriente por el resistor de 220 ohm hacia las bases
         *    del par complementario C1815/A1015. Luego se incrementa 's_pcm_index' (n = n + 1) para dejar
         *    preparada la muestra del instante t_{n+1} = (n + 1) * 125 us.
         */
        ledc_set_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL, duty_8bit);
        ledc_update_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL);
        s_pcm_index++;
    } else {
        /*
         * 5. Condición de parada al agotarse las muestras del vector (s_pcm_index == s_pcm_length):
         *    Detiene el temporizador periódico de 125 us y lleva el ciclo de trabajo del pin GPIO_23 a 0 (0V).
         *    En 0V, el transistor NPN (C1815) queda en corte absoluto, eliminando cualquier consumo de
         *    corriente en reposo desde el capacitor tanque de 1000 uF y la línea de +5V.
         */
        s_reproduciendo_pcm = false;
        s_tono_encendido = false;
        esp_timer_stop(s_pcm_timer_handle);
        ledc_set_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL, 0);
        ledc_update_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL);
    }
}

/**
 * @brief Tabla de frecuencias base en Hertz para las 12 notas cromáticas de la Octava 4 (Escala Temperada, La4 = 440 Hz).
 * @details Índices: 0=Do(c), 1=Do#(c#), 2=Re(d), 3=Re#(d#), 4=Mi(e), 5=Fa(f), 6=Fa#(f#), 7=Sol(g), 8=Sol#(g#), 9=La(a), 10=La#(a#), 11=Si(b).
 *          Las frecuencias de octavas superiores o inferiores se obtienen por desplazamiento binario de bits (multiplicación o división por potencias de 2).
 */
static const uint16_t s_rtttl_notes_o4[] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494 // c, c#, d, d#, e, f, f#, g, g#, a, a#, b
};

/**
 * @brief Callback del secuenciador RTTTL que decodifica la siguiente nota musical de la cadena,
 *        configura su frecuencia en el periférico LEDC y reprograma el temporizador por la duración de la figura.
 * @param[in] arg Argumento genérico del temporizador (no utilizado).
 */
static void rtttl_timer_callback(void *arg) {
    // Si la reproducción fue cancelada o se alcanzó el terminador nulo '\0' de la cadena, apaga el audio
    if (!s_reproduciendo_rtttl || s_rtttl_ptr == NULL || *s_rtttl_ptr == '\0') {
        AvisoSonoroDetenerRtttl();
        return;
    }

    // Avanza el puntero saltando los delimitadores de coma (',') y espacios en blanco entre notas
    while (*s_rtttl_ptr == ',' || *s_rtttl_ptr == ' ') s_rtttl_ptr++;
    if (*s_rtttl_ptr == '\0') {
        AvisoSonoroDetenerRtttl();
        return;
    }

    /*
     * 1. Decodificación del divisor de duración de la figura musical (1=redonda, 2=blanca, 4=negra, 8=corchea, 16=semicorchea, 32=fusa):
     *    Lee los dígitos ASCII iniciales convirtiéndolos a entero mediante (dur * 10 + (*s_rtttl_ptr - '0')).
     *    Si la nota no especifica un número delante de la letra, adopta la duración por defecto del encabezado (s_default_dur).
     */
    uint8_t dur = 0;
    while (isdigit((unsigned char)*s_rtttl_ptr)) {
        dur = dur * 10 + (*s_rtttl_ptr - '0');
        s_rtttl_ptr++;
    }
    if (dur == 0) dur = s_default_dur;

    /*
     * 2. Extracción del carácter de la nota musical ('c', 'd', 'e', 'f', 'g', 'a', 'b', o 'p' para pausa/silencio)
     *    y verificación del modificador de alteración sostenido ('#' = +1 semitono cromático).
     */
    char note = tolower((unsigned char)*s_rtttl_ptr);
    s_rtttl_ptr++;
    bool sharp = false;
    if (*s_rtttl_ptr == '#') {
        sharp = true;
        s_rtttl_ptr++;
    }

    /*
     * 3. Verificación de puntillo musical ('.') antes o después del número de octava:
     *    En notación RTTTL el puntillo (que prolonga la duración de la nota en un 50%, es decir, factor 1.5x)
     *    puede aparecer inmediatamente después de la letra de la nota o después del dígito de la octava.
     */
    bool dot = false;
    if (*s_rtttl_ptr == '.') {
        dot = true;
        s_rtttl_ptr++;
    }

    /*
     * 4. Extracción del número de octava (4, 5, 6, 7):
     *    Si no se especifica un dígito, se emplea la octava por defecto del encabezado (s_default_oct).
     */
    uint8_t scale = s_default_oct;
    if (isdigit((unsigned char)*s_rtttl_ptr)) {
        scale = *s_rtttl_ptr - '0';
        s_rtttl_ptr++;
    }
    if (*s_rtttl_ptr == '.') {
        dot = true;
        s_rtttl_ptr++;
    }

    /*
     * 5. Cálculo del tiempo de duración físico de la nota en milisegundos:
     *    - Una negra (dur = 4) dura (60.000 ms / s_bpm).
     *    - Una redonda (dur = 1) dura 4 negras = (60.000 * 4) / s_bpm.
     *    - Por lo tanto, cualquier figura con divisor 'dur' dura: duration_ms = (240.000) / (s_bpm * dur).
     *    - Si posee puntillo (dot == true), se multiplica por 3/2 (1.5x) mediante aritmética entera.
     */
    uint32_t duration_ms = (60000UL * 4) / (s_bpm * dur);
    if (dot) duration_ms = (duration_ms * 3) / 2;

    /*
     * 6. Cálculo de la frecuencia acústica en Hertz mediante desplazamiento de bits por octava:
     *    - Si la nota es una pausa ('p'), freq_hz queda en 0 Hz (silencio).
     *    - Si es una nota tonal, se mapea la letra a su índice base en la tabla de la Octava 4 (s_rtttl_notes_o4).
     *    - Si tiene sostenido ('#'), se incrementa el índice en 1 semitono.
     *    - Como subir una octava equivale a duplicar la frecuencia (x2^k) y bajar una octava a dividirla por dos (/2^k),
     *      se utiliza el operador de desplazamiento de bits a la izquierda (f <<= (scale - 4)) para octavas >= 4,
     *      o desplazamiento a la derecha (f >>= (4 - scale)) para octavas < 4.
     */
    uint32_t freq_hz = 0;
    if (note != 'p') {
        int note_idx = -1;
        switch (note) {
            case 'c': note_idx = 0; break;
            case 'd': note_idx = 2; break;
            case 'e': note_idx = 4; break;
            case 'f': note_idx = 5; break;
            case 'g': note_idx = 7; break;
            case 'a': note_idx = 9; break;
            case 'b': note_idx = 11; break;
            default: break;
        }
        if (note_idx >= 0) {
            if (sharp && note_idx < 11) note_idx++;
            uint32_t f = s_rtttl_notes_o4[note_idx];
            if (scale >= 4) {
                f <<= (scale - 4);
            } else {
                f >>= (4 - scale);
            }
            freq_hz = f;
        }
    }

    // 7. Aplica la frecuencia con ciclo de trabajo del 50% (128) si es nota audible, o apaga el PWM (duty = 0) si es silencio ('p')
    if (freq_hz > 0) {
        aplicar_frecuencia_y_duty(freq_hz, AVISO_PCM_NEUTRAL_DUTY);
    } else {
        ledc_set_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL, 0);
        ledc_update_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL);
    }

    // 8. Programa el temporizador de disparo único (en microsegundos: duration_ms * 1000) para procesar la siguiente nota al vencer el tiempo
    esp_timer_start_once(s_rtttl_timer_handle, duration_ms * 1000ULL);
}

/*==================[external functions definition]==========================*/
/**
 * @brief Inicializa el periférico LEDC y los temporizadores de hardware para síntesis LPCM y melodías RTTTL.
 * @param[in] pin Pin GPIO conectado a la base de la etapa Push-Pull (GPIO_23).
 */
void AvisoSonoroInit(gpio_t pin) {
    s_audio_pin = pin;

    /*
     * 1. Configuración del Temporizador 0 del periférico LEDC:
     *    - duty_resolution = LEDC_TIMER_8_BIT: Define un contador de 8 bits (256 pasos de ancho de pulso).
     *    - clk_cfg = LEDC_USE_XTAL_CLK: Selecciona el cristal oscilador de 40 MHz como fuente de reloj,
     *      lo cual permite abarcar desde notas graves (~130 Hz) hasta la portadora ultrasónica LPCM de 32 kHz.
     */
    ledc_timer_config_t timer_conf = {
        .speed_mode       = AVISO_LEDC_MODE,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .timer_num        = AVISO_LEDC_TIMER,
        .freq_hz          = AVISO_FREQ_ALARMA_FIFO_HZ,
        .clk_cfg          = LEDC_USE_XTAL_CLK
    };
    ledc_timer_config(&timer_conf);
    s_freq_actual_hz = AVISO_FREQ_ALARMA_FIFO_HZ;

    /*
     * 2. Configuración del Canal 0 de LEDC sobre GPIO_23:
     *    Se inicializa con ciclo de trabajo .duty = 0 (salida en 0.0V) para asegurar que el parlante
     *    permanezca en silencio absoluto al arrancar el microcontrolador.
     */
    ledc_channel_config_t channel_conf = {
        .gpio_num       = (int)s_audio_pin,
        .speed_mode     = AVISO_LEDC_MODE,
        .channel        = AVISO_LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = AVISO_LEDC_TIMER,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&channel_conf);
    s_tono_encendido = false;

    /*
     * 3. Creación del temporizador de alta resolución (SYSTIMER) para muestreo LPCM a 8000 Hz (125 us):
     *    Vincula el callback pcm_sample_timer_callback() que recorrerá el arreglo de voz muestra a muestra.
     */
    if (s_pcm_timer_handle == NULL) {
        const esp_timer_create_args_t pcm_timer_args = {
            .callback = &pcm_sample_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "pcm_8khz_timer"
        };
        esp_timer_create(&pcm_timer_args, &s_pcm_timer_handle);
    }

    /*
     * 4. Creación del temporizador de alta resolución para el secuenciador no bloqueante de notas RTTTL:
     *    Vincula el callback rtttl_timer_callback() que cambiará de nota según el tempo BPM de cada melodía.
     */
    if (s_rtttl_timer_handle == NULL) {
        const esp_timer_create_args_t rtttl_timer_args = {
            .callback = &rtttl_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "rtttl_melody_timer"
        };
        esp_timer_create(&rtttl_timer_args, &s_rtttl_timer_handle);
    }
}

/**
 * @brief Activa un tono continuo de onda cuadrada (50% duty) si no hay locución LPCM ni melodía RTTTL activa.
 * @param[in] freq_hz Frecuencia del tono en Hertz.
 */
void AvisoSonoroEncenderTono(uint32_t freq_hz) {
    // Respeta la prioridad de la voz LPCM y de las melodías RTTTL evitando sobrescribir el temporizador LEDC
    if (s_reproduciendo_pcm || s_reproduciendo_rtttl || freq_hz == 0) return;
    if (!s_tono_encendido || freq_hz != s_freq_actual_hz) {
        aplicar_frecuencia_y_duty(freq_hz, AVISO_PCM_NEUTRAL_DUTY);
        s_tono_encendido = true;
    }
}

/**
 * @brief Apaga el tono continuo llevando la salida PWM a 0% si no hay voz ni melodía en curso.
 */
void AvisoSonoroApagar(void) {
    if (s_reproduciendo_pcm || s_reproduciendo_rtttl) return;
    if (s_tono_encendido) {
        ledc_set_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL, 0);
        ledc_update_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL);
        s_tono_encendido = false;
    }
}

/**
 * @brief Analiza el encabezado de una cadena RTTTL e inicia su ejecución asíncrona en segundo plano.
 * @param[in] rtttl_melody Puntero a la cadena en formato "Nombre:d=...,o=...,b=...:notas".
 */
void AvisoSonoroReproducirRtttl(const char *rtttl_melody) {
    if (rtttl_melody == NULL || s_rtttl_timer_handle == NULL) return;
    // Detiene cualquier melodía o locución previa antes de iniciar la nueva partitura
    AvisoSonoroDetenerRtttl();
    if (s_reproduciendo_pcm) {
        esp_timer_stop(s_pcm_timer_handle);
        s_reproduciendo_pcm = false;
    }

    /*
     * 1. Salto de la primera sección (Nombre de la canción) hasta encontrar el primer separador ':':
     */
    const char *p = rtttl_melody;
    while (*p != ':' && *p != '\0') p++;
    if (*p == ':') p++;

    // Valores estándar por defecto según la especificación RTTTL
    s_default_dur = 4; s_default_oct = 6; s_bpm = 63;

    /*
     * 2. Lectura de la segunda sección (Parámetros globales d=duración, o=octava, b=tempo en BPM)
     *    hasta encontrar el segundo separador ':':
     */
    while (*p != ':' && *p != '\0') {
        while (*p == ' ') p++;
        if (*p == 'd') { p += 2; s_default_dur = atoi(p); }
        else if (*p == 'o') { p += 2; s_default_oct = atoi(p); }
        else if (*p == 'b') { p += 2; s_bpm = atoi(p); }
        while (*p != ',' && *p != ':' && *p != '\0') p++;
        if (*p == ',') p++;
    }
    if (*p == ':') p++;

    /*
     * 3. Posiciona el puntero global s_rtttl_ptr al comienzo de la secuencia de notas
     *    y dispara inmediatamente la primera nota invocando rtttl_timer_callback().
     */
    s_rtttl_ptr = p;
    s_reproduciendo_rtttl = true;
    rtttl_timer_callback(NULL);
}

/**
 * @brief Detiene inmediatamente la reproducción de la melodía RTTTL en curso y apaga la etapa de potencia.
 */
void AvisoSonoroDetenerRtttl(void) {
    if (s_reproduciendo_rtttl) {
        s_reproduciendo_rtttl = false;
        esp_timer_stop(s_rtttl_timer_handle);
        ledc_set_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL, 0);
        ledc_update_duty(AVISO_LEDC_MODE, AVISO_LEDC_CHANNEL);
        s_tono_encendido = false;
    }
}

/**
 * @brief Informa si hay una melodía RTTTL ejecutándose en este instante.
 * @return true si está sonando; false en caso contrario.
 */
bool AvisoSonoroReproduciendoRtttl(void) {
    return s_reproduciendo_rtttl;
}

/**
 * @brief Inicia la reproducción asíncrona de un vector de voz LPCM de 8 bits a 8000 Hz sobre portadora de 32 kHz.
 * @param[in] pcm_data Puntero al arreglo de muestras con signo en memoria Flash.
 * @param[in] num_samples Cantidad total de muestras del arreglo.
 */
void AvisoSonoroReproducirPcm(const int8_t *pcm_data, uint32_t num_samples) {
    if (s_pcm_timer_handle == NULL || pcm_data == NULL || num_samples == 0) return;
    // La locución hablada tiene prioridad máxima: detiene cualquier melodía RTTTL o reproducción previa
    AvisoSonoroDetenerRtttl();
    s_reproduciendo_pcm = false;
    esp_timer_stop(s_pcm_timer_handle);

    /*
     * Inicialización de los punteros de recorrido temporal del vector de audio:
     * - s_pcm_ptr apunta a la muestra inicial x[0] (instante t = 0 s).
     * - s_pcm_length almacena la cantidad total de muestras N (duración física = N * 125 us).
     * - s_pcm_index se pone a 0 para comenzar el barrido desde el primer elemento.
     */
    s_pcm_ptr = pcm_data;
    s_pcm_length = num_samples;
    s_pcm_index = 0;
    s_tono_encendido = false;

    /*
     * Eleva la frecuencia del periférico LEDC a la portadora ultrasónica Clase D de 32.000 Hz
     * con ciclo de trabajo neutro inicial (128 = 50%) y arranca el temporizador periódico
     * que ejecutará pcm_sample_timer_callback() exactamente cada 125 microsegundos (8000 Hz).
     */
    aplicar_frecuencia_y_duty(AVISO_PCM_CARRIER_HZ, AVISO_PCM_NEUTRAL_DUTY);
    s_reproduciendo_pcm = true;
    esp_timer_start_periodic(s_pcm_timer_handle, AVISO_PCM_SAMPLE_PERIOD_US);
}

/**
 * @brief Reproduce la locución "Ayuda en camino" invocando el vector constante alojado en Flash.
 */
void AvisoSonoroReproducirVozAyuda(void) {
    // Invoca mediante la macro AVISO_SONORO_PLAY el arreglo definido en "sonidos_lpcm/ayuda_en_camino.h"
    AVISO_SONORO_PLAY(ayuda_en_camino_pcm);
}

/**
 * @brief Informa si el motor LPCM se encuentra reproduciendo la voz de asistencia actualmente.
 * @return true si la voz está en curso; false si finalizó.
 */
bool AvisoSonoroReproduciendoVoz(void) {
    return s_reproduciendo_pcm;
}

/*==================[end of file]============================================*/