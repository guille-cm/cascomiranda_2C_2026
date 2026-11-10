/**
 * @file display_16x2_i2c.c
 * @brief Implementación del driver para display LCD 16x2 mediante conversor I2C PCF8574.
 * 
 * @details Emplea el driver oficial i2c_mcu de la cátedra para gestionar la comunicación
 *          con el expansor PCF8574 y el controlador HD44780, utilizando retardos no
 *          bloqueantes de FreeRTOS y el propio tiempo de trama del bus I2C a 100 kHz. @n
 * 
 * @section i2c_addressing Detalle de Direccionamiento I2C (0x27 y 0x4E)
 * La comunicación física entre el microcontrolador ESP32-C6 y el expansor PCF8574 requiere 
 * comprender la codificación del byte de direccionamiento en el bus I2C:
 * - <b>Dirección base de 7 bits (0x27):</b> El circuito integrado PCF8574 posee una dirección fija de esclavo 
 *   que en formato binario de 7 bits se representa como \c 0100111.
 * - <b>Desplazamiento y Byte en el Bus (0x4E):</b> Al transmitir la dirección por el bus físico, el microcontrolador 
 *   desplaza los 7 bits hacia la izquierda y añade un 8.° bit (LSB) que define la operación de Lectura o Escritura 
 *   (\c R/W). Al tratarse de una operación de Escritura, el bit LSB se fija en \c 0. 
 *   Esto resulta en la secuencia binaria \c 01001110, la cual equivale al valor hexadecimal <b>0x4E</b> observado 
 *   en el osciloscopio al inicio de cada trama. Véase la captura [i2c_todos_los_pulsos_de_prueba.png](../i2c_todos_los_pulsos_de_prueba.png).
 * 
 * @section byte_processing Transformación de Comandos y Caracteres y Justificación del Envío Duplicado
 * ### <b>1. Formación del Byte hacia el PCF8574 (Desplazamientos y Máscaras)</b>
 * Todo byte de 8 bits destinado al controlador HD44780 (sea un comando con \c is_data=false o un carácter ASCII con \c is_data=true)
 * es procesado por la cadena de funciones \c send_byte() -> \c send_nibble() -> \c pcf8574_write():
 * 1. <b>División en dos cuartetos (nibbles):</b> En \c send_byte(), el byte original se divide en su nibble superior (\c value >> 4)
 *    y su nibble inferior (\c value & 0x0F).
 * 2. <b>Alineación a los pines D7..D4 (P7..P4):</b> En \c send_nibble(), el cuarteto de 4 bits se desplaza 4 posiciones hacia la
 *    izquierda (\c (nibble & 0x0F) << 4) para ocupar los bits 7 a 4 del puerto del expansor PCF8574.
 * 3. <b>Inserción de bits de control en P3..P0:</b> En los 4 bits inferiores se combinan mediante operaciones OR bit a bit las
 *    máscaras de control: \c MASK_RS (\c 0x01 en P0: \c 0 para comando, \c 1 para carácter ASCII), \c MASK_RW (\c 0x02 en P1: siempre \c 0),
 *    \c MASK_EN (\c 0x04 en P2: pulso de validación) y \c MASK_BL (\c 0x08 en P3: retroiluminación activa en \c 1).
 * 4. <b>Generación del flanco de bajada de Enable:</b> Por cada nibble se realizan dos llamadas sucesivas a \c pcf8574_write():
 *    la primera con \c data | MASK_EN (\c P2=1) y la segunda con \c data & ~MASK_EN (\c P2=0). El flanco descendente en el pin 6 (\c E)
 *    del display ordena al HD44780 capturar los 4 bits presentes en los pines 11 a 14 (\c D4..D7).
 *    - <i>Ejemplo para el comando \c 0x80 (Set Cursor Fila 0, Col 0):</i> Nibble alto \c 0x8 -> envía \c 0x8C (\c 10001100, \c EN=1) y
 *      \c 0x88 (\c 10001000, \c EN=0); Nibble bajo \c 0x0 -> envía \c 0x0C (\c 00001100, \c EN=1) y \c 0x08 (\c 00001000, \c EN=0),
 *      correspondientes exactamente a las Ventanas 4, 5, 6 y 7 de [i2c_todos_los_pulsos_de_prueba.png](../i2c_todos_los_pulsos_de_prueba.png).
 *    - <i>Ejemplo para el carácter \c 'B' (\c 0x42 = \c 01000010):</i> Al ser dato (\c RS=1), el nibble alto \c 0x4 genera \c 0x4D (\c EN=1)
 *      y \c 0x49 (\c EN=0); el nibble bajo \c 0x2 genera \c 0x2D (\c EN=1) y \c 0x29 (\c EN=0).
 * 
 * ### <b>2. Motivo de la Duplicación del Byte en cada Trama I2C y Eficiencia en el Sistema</b>
 * En las capturas de osciloscopio se observa que cada trama consta de 3 bytes: \c [0x4E] + \c [dato] + \c [dato].
 * Esto se debe a que la función oficial \c I2C_writeByte(uint8_t devAddr, uint8_t regAddr, uint8_t data) del driver \c i2c_mcu.c
 * de la cátedra fue concebida para sensores con registros internos direccionables, empaquetando un arreglo de 2 bytes
 * \c buffer[2] = {regAddr, data} tras la dirección del esclavo. Como el expansor PCF8574 carece de registros internos y vuelca
 * cada byte recibido directamente a sus pines \c P7..P0 tras cada pulso de \c ACK, al pasar \c data tanto en \c regAddr como
 * en \c data (\c I2C_writeByte(LCD_I2C_ADDR, data, data)) el expansor fija sus salidas en el primer byte y mantiene exactamente
 * el mismo estado lógico durante el segundo byte sin generar transiciones espurias.
 * 
 * Desde el punto de vista del desempeño en el proyecto de monitoreo de unidad de paciente, esta estrategia preserva intacta
 * la capa de abstracción \c i2c_mcu.c de la cátedra, garantiza un ancho de pulso \c Enable de ~180 µs regulado por el propio reloj
 * I2C sin consumir temporizadores de hardware (\c gptimer), e insume apenas ~58 ms cada 1000 ms (menos del 6% de ocupación del bus) en una
 * tarea de baja prioridad que es preempcionada de forma transparente cada 2 ms por las tareas de 500 Hz del ECG y Respirograma.
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-07 | Ajuste de implementación a la firma de I2C_writeByte (devAddr, regAddr, data) |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include "display_16x2_i2c.h"
#include <stddef.h>

/*==================[macros and definitions]=================================*/
#define I2C_CLOCK_RATE_HZ   100000  /*!< Frecuencia del bus I2C estándar (100 kHz) */

#define MASK_RS             0x01    /*!< P0 -> RS (0: Comando, 1: Dato) */
#define MASK_RW             0x02    /*!< P1 -> RW (Siempre 0 para escritura) */
#define MASK_EN             0x04    /*!< P2 -> Enable */
#define MASK_BL             0x08    /*!< P3 -> Backlight */

#define CMD_CLEAR_DISPLAY   0x01    /*!< Comando HD44780: Limpia la pantalla y lleva el cursor a (0,0) */
#define CMD_ENTRY_MODE_SET  0x06    /*!< Comando HD44780: Incrementa el cursor automáticamente y sin desplazamiento de display */
#define CMD_DISPLAY_ON      0x0C    /*!< Comando HD44780: Enciende el display, oculta el cursor y desactiva el parpadeo */
#define CMD_FUNCTION_SET_4B 0x28    /*!< Comando HD44780: Configura interfaz de 4 bits, 2 líneas y matriz de 5x8 puntos */
#define CMD_SET_DDRAM_ROW0  0x80    /*!< Comando HD44780: Dirección base de la memoria DDRAM para la Fila 0 */
#define CMD_SET_DDRAM_ROW1  0xC0    /*!< Comando HD44780: Dirección base de la memoria DDRAM para la Fila 1 */

/*==================[internal data declaration]==============================*/
/**
 * @brief Variable estática de retención del estado de la luz de fondo (Backlight).
 * @details Dado que el expansor PCF8574 carece de máscaras de modificación parcial por hardware y actualiza
 *          simultáneamente sus 8 pines (P7..P0) en cada escritura I2C, esta variable conserva el estado
 *          de encendido/apagado para superponer el bit MASK_BL (0x08) en cada transferencia de datos o comandos.
 */
static bool backlight_state = true;

/*==================[internal functions declaration]=========================*/
static void pcf8574_write(uint8_t data);
static void send_nibble(uint8_t nibble, bool is_data);
static void send_byte(uint8_t value, bool is_data);
static void send_command(uint8_t cmd);

/*==================[internal functions definition]==========================*/
/**
 * @brief Transmite un byte de datos al expansor I2C PCF8574.
 * @details Al carecer el circuito integrado PCF8574 de registros internos direccionables,
 *          se transmite el byte replicado en ambos parámetros exigidos por la función \c I2C_writeByte().
 *          Esto genera en el bus I2C una trama de 27 bits: \c [Start][0x4E][ACK][data][ACK][data][ACK][Stop].
 * @param[in] data Byte de 8 bits que define el estado de los pines de salida \c P7..P0 del expansor.
 */
static void pcf8574_write(uint8_t data) {
    /* 
     * Invoca la primitiva de escritura I2C pasando la dirección de 7 bits LCD_I2C_ADDR (0x27,
     * que en el bus físico se transmite como (0x27 << 1) | 0 = 0x4E).
     * Como el PCF8574 no posee registros internos direccionables, se duplica el argumento 'data'
     * tanto en el campo de dirección de registro como en el de dato útil.
     * El PCF8574 vuelca el primer byte 'data' a sus pines P7..P0 en el primer ACK y ratifica
     * exactamente el mismo valor binario en el segundo ACK. A una frecuencia de reloj SCL de
     * 100 kHz, esta transacción completa de 27 bits insume ~270 us, proporcionando de forma
     * intrínseca el tiempo de establecimiento requerido por el controlador HD44780 sin bloquear
     * temporizadores de hardware.
     */
    I2C_writeByte(LCD_I2C_ADDR, data, data);
}

/**
 * @brief Envía un cuarteto de 4 bits al controlador del display a través del expansor I2C.
 * @details Desplaza el cuarteto recibido hacia los bits superiores del puerto (\c P7..P4 -> \c D7..D4),
 *          configura la línea de selección de registro (\c P0 -> \c RS) y el estado de la retroiluminación
 *          (\c P3 -> \c BL), y genera un pulso estroboscópico en el pin de habilitación (\c P2 -> \c EN)
 *          mediante dos transferencias I2C sucesivas (\c EN=1 seguido de \c EN=0).
 * @param[in] nibble Cuarteto de 4 bits a transmitir en los bits inferiores (\c 0x00 a \c 0x0F).
 * @param[in] is_data Indicador de tipo de información: \c false para comandos (\c RS=0), \c true para caracteres (\c RS=1).
 */
static void send_nibble(uint8_t nibble, bool is_data) {
    /*
     * 1. Enmascaramiento y alineación del cuarteto de datos hacia el nibble superior (bits 7..4):
     *    - La operación (nibble & 0x0F) limpia por seguridad los 4 bits superiores (bits 7..4 = 0000),
     *      reteniendo únicamente los 4 bits inferiores útiles (bits 3..0).
     *    - El desplazamiento de 4 posiciones hacia la izquierda (<< 4) traslada esos 4 bits a las
     *      posiciones 7..4 del byte 'data', las cuales corresponden físicamente a las salidas P7..P4
     *      del PCF8574 (conectadas a los pines D7..D4 del módulo display 1602A Ver5.5).
     *    - Los 4 bits inferiores de 'data' (bits 3..0, salidas P3..P0) quedan inicialmente en 0000
     *      (lo que garantiza implícitamente que el bit P1 = MASK_RW permanezca en 0 = Modo Escritura).
     */
    uint8_t data = (uint8_t)((nibble & 0x0F) << 4);

    /*
     * 2. Configuración del bit P0 (Selección de Registro RS):
     *    - Si 'is_data' es true (envío de un carácter ASCII a la memoria gráfica DDRAM), se aplica
     *      una operación OR bit a bit con MASK_RS (0x01 = 00000001b), poniendo P0 en nivel ALTO (1).
     *    - Si 'is_data' es false (envío de una instrucción de control), el bit P0 permanece en BAJO (0).
     */
    if (is_data) {
        data |= MASK_RS; // Activa el bit 0 (P0 -> RS = 1) para seleccionar el Registro de Datos
    }

    /*
     * 3. Retención del bit P3 (Control de Retroiluminación BL):
     *    - Si la variable de estado 'backlight_state' es true, se aplica una operación OR bit a bit
     *      con MASK_BL (0x08 = 00001000b) para mantener polarizado el transistor que enciende el LED
     *      de luz de fondo del display durante toda la transferencia.
     */
    if (backlight_state) {
        data |= MASK_BL; // Activa el bit 3 (P3 -> BL = 1) manteniendo encendida la retroiluminación
    }

    /*
     * 4. Primera transferencia I2C (Establecimiento de datos y flanco ascendente de Enable):
     *    - La operación OR bit a bit (data | MASK_EN) pone en 1 el bit 2 (0x04 = 00000100b -> P2 = 1).
     *    - Al transmitirse por I2C, el PCF8574 presenta simultáneamente el cuarteto de datos en D7..D4,
     *      el nivel de RS en el pin 4 y eleva el pin 6 (Enable) a +5V durante ~270 us.
     */
    pcf8574_write(data | MASK_EN);

    /*
     * 5. Segunda transferencia I2C (Flanco descendente de Enable y captura en el HD44780):
     *    - El operador de complemento a uno (~MASK_EN) invierte los bits de 0x04 (00000100b),
     *      produciendo la máscara 0xFB (11111011b).
     *    - La operación AND bit a bit (data & ~MASK_EN) fuerza a 0 únicamente el bit 2 (P2 -> EN = 0),
     *      conservando intactos los bits de datos (P7..P4), el bit de Backlight (P3) y el bit RS (P0).
     *    - Esta transición de 1 a 0 en el pin Enable genera el flanco descendente que ordena al
     *      controlador HD44780 latchear internamente el cuarteto presente en D7..D4.
     */
    pcf8574_write(data & (uint8_t)(~MASK_EN));
}

/**
 * @brief Envía un byte completo de 8 bits dividiéndolo en dos operaciones secuenciales de cuartetos.
 * @details Desglosa el valor recibido llamando a la función \c send_nibble: primero transmite 
 *          el cuarteto superior (bits 7..4) y posteriormente el cuarteto inferior (bits 3..0),
 *          produciendo un total de 4 tramas I2C por cada byte enviado al controlador HD44780.
 * @param[in] value Byte completo de 8 bits a transmitir.
 * @param[in] is_data Indicador de tipo de información: \c false para comandos, \c true para caracteres.
 */
static void send_byte(uint8_t value, bool is_data) {
    /*
     * En modo de interfaz de 4 bits, el protocolo del controlador HD44780 exige recibir primero
     * los 4 bits más significativos (nibble alto, bits 7..4) y luego los 4 bits menos significativos
     * (nibble bajo, bits 3..0):
     * 1. (value >> 4): Desplaza el byte 4 posiciones hacia la derecha, ubicando los bits 7..4
     *    en las posiciones 3..0 para ser procesados y enviados en primer término por send_nibble().
     */
    send_nibble(value >> 4, is_data);

    /*
     * 2. (value & 0x0F): Aplica una máscara AND con 00001111b para aislar los bits 3..0 originales
     *    del byte y transmitirlos en segundo término, completando la reconstrucción del byte de 8 bits
     *    dentro del registro interno del controlador HD44780.
     */
    send_nibble(value & 0x0F, is_data);
}

/**
 * @brief Envía un comando de control al controlador del display LCD.
 * @details Envía el byte especificado configurando la línea de selección de registro en nivel bajo (\c RS=0).
 * @param[in] cmd Byte correspondiente al comando de control del HD44780.
 */
static void send_command(uint8_t cmd) {
    /* Invoca send_byte fijando is_data = false para direccionar el Registro de Instrucciones (RS = 0) */
    send_byte(cmd, false);
}

/*==================[external functions definition]==========================*/
/**
 * @brief Inicializa el bus I2C y ejecuta la secuencia estricta de configuración del controlador HD44780
 *        para el display LCD 16x2 modelo 1602A Ver5.5.
 * @details El módulo display **1602A Ver5.5** posee 16 terminales en el orden físico:
 *          \c 1:GND, \c 2:VDD, \c 3:VO, \c 4:RS, \c 5:RW, \c 6:E, \c 7..10:D0..D3 (no conectados en modo 4 bits),
 *          \c 11..14:D4..D7 (conectados a \c P4..P7 del PCF8574), \c 15:BLA (ánodo de backlight a +5V) y
 *          \c 16:BLK (cátodo de backlight conmutado a masa por el transistor asociado a \c P3 del PCF8574).
 *          
 *          Dado que tras el encendido o tras un reinicio en caliente del microcontrolador el controlador HD44780
 *          puede hallarse en modo de 8 bits o desfasado a mitad de una transferencia de 4 bits, se aplica el
 *          procedimiento de inicialización por software (*Reset by Instruction*) especificado por la hoja de datos:
 *          1. **Estabilización inicial:** Espera 100 ms tras el encendido para asegurar que \c VDD alcance los 5V nominales.
 *          2. **Reseteo por software (Modo 8 bits forzado):** Envía el comando preliminar \c 0x03 tres veces consecutivas 
 *             (con retardos de 10 ms y 2 ms) para sincronizar el controlador interno a interfaz de 8 bits sin importar su estado previo.
 *          3. **Conmutación a interfaz de 4 bits:** Envía el cuarteto \c 0x02 para configurar formalmente el bus de datos a 4 bits
 *             sobre las líneas \c D4..D7 (pines 11 a 14 del módulo 1602A Ver5.5).
 *          4. **Configuración de parámetros del display:** Selecciona el funcionamiento 
 *             en modo de 4 bits, con matrices de 5x8 puntos y 2 líneas de visualización.  
 *               \c CMD_FUNCTION_SET_4B / \c 0x28 @n
 *          5. **Encendido general:** Activa el display, ocultando el cursor parpadeante.  
 *               \c CMD_DISPLAY_ON / \c 0x0C @n
 *          6. **Limpieza de memoria DDRAM:** Borra caracteres previos y posiciona el cursor en (0,0).  
 *               \c Display16x2I2cClear @n
 *          7. **Modo de entrada:** Configura el incremento automático del cursor hacia la derecha.  
 *               \c CMD_ENTRY_MODE_SET / \c 0x06 @n
 * @return true al finalizar exitosamente la inicialización.
 */
bool Display16x2I2cInit(void) {
    // Configura el periférico I2C maestro del ESP32-C6 a una velocidad de reloj SCL de 100 kHz
    I2C_initialize(I2C_CLOCK_RATE_HZ);
    // Cede la CPU en estado Blocked durante 100 ms hasta que la fuente de +5V supere los 4.5V requeridos por el HD44780
    vTaskDelay(pdMS_TO_TICKS(100));

    // Paso 1 de sincronización (Reset por instrucción): Envía el nibble 0x03 (D7..D4 = 0011b, equivalente a Function Set 8 bits)
    // Trama en bus I2C: [0x4E, 0x3C, 0x3C] (EN=1) seguido de [0x4E, 0x38, 0x38] (EN=0)
    send_nibble(0x03, false);
    // Espera de 10 ms en estado Blocked (la hoja de datos del HD44780 exige un mínimo de 4.1 ms tras el primer 0x03)
    vTaskDelay(pdMS_TO_TICKS(10));

    // Paso 2 de sincronización: Segundo envío del nibble 0x03 para asegurar alineación si el display estaba a mitad de un byte en 4 bits
    send_nibble(0x03, false);
    // Espera de 2 ms en estado Blocked (la hoja de datos exige un mínimo de 100 us tras el segundo 0x03)
    vTaskDelay(pdMS_TO_TICKS(2));

    // Paso 3 de sincronización: Tercer envío del nibble 0x03; a partir de aquí el HD44780 se encuentra garantizadamente en modo de 8 bits
    send_nibble(0x03, false);
    // Espera de 2 ms antes de conmutar el ancho de bus
    vTaskDelay(pdMS_TO_TICKS(2));

    // Conmutación definitiva a interfaz de 4 bits: Envía un único nibble 0x02 (D7..D4 = 0010b -> DL = 0)
    // Trama en bus I2C: [0x4E, 0x2C, 0x2C] (EN=1) seguido de [0x4E, 0x28, 0x28] (EN=0)
    send_nibble(0x02, false);
    // Espera de 2 ms para que el controlador interno reconfigure su máquina de estados a recepción por pares de nibbles
    vTaskDelay(pdMS_TO_TICKS(2));

    // Envía ya en modo de 4 bits (2 nibbles por comando) la configuración de formato 0x28 (DL=0: 4 bits, N=1: 2 filas, F=0: 5x8 puntos)
    send_command(CMD_FUNCTION_SET_4B);
    // Envía el comando 0x0C (D=1: enciende cristal líquido, C=0: apaga subrayado de cursor, B=0: apaga parpadeo de celda)
    send_command(CMD_DISPLAY_ON);
    // Ejecuta el comando 0x01 para escribir el código ASCII 0x20 (espacio) en las 80 posiciones de la memoria DDRAM y esperar 5 ms
    Display16x2I2cClear();
    // Envía el comando 0x06 (I/D=1: incrementa automáticamente la dirección DDRAM tras cada carácter, S=0: pantalla estática)
    send_command(CMD_ENTRY_MODE_SET);

    return true;
}

/**
 * @brief Borra todo el contenido visualizado en el display LCD y retorna el cursor a la posición inicial.
 * @details Envía el comando de limpieza y aplica un retardo temporal para permitir que el controlador 
 *          ejecute el borrado de la memoria DDRAM.
 */
void Display16x2I2cClear(void) {
    // Transmite el comando 0x01 (nibble alto 0x0 y nibble bajo 0x1 con RS=0)
    send_command(CMD_CLEAR_DISPLAY);
    // La instrucción Clear Display requiere 1.52 ms internos en el HD44780; se bloquea la tarea por 5 ms cediendo la CPU
    vTaskDelay(pdMS_TO_TICKS(5));
}

/**
 * @brief Posiciona el cursor de escritura en una coordenada específica de la pantalla.
 * @details Calcula la dirección de la memoria DDRAM en función de la fila y columna seleccionadas, 
 *          asegurando que los límites se mantengan dentro del rango válido del display.
 * @param[in] row Fila de destino (\c LCD_ROW_0 o \c LCD_ROW_1).
 * @param[in] col Columna de destino (0 a 15).
 */
void Display16x2I2cSetCursor(uint8_t row, uint8_t col) {
    // Acota por seguridad el índice de columna al límite máximo físico de la pantalla (columna 15)
    if (col >= LCD_COLS) {
        col = LCD_COLS - 1;
    }
    /*
     * Cálculo de la instrucción Set DDRAM Address (bit 7 = 1, bits 6..0 = dirección de celda):
     * - En el controlador HD44780, las 16 celdas visibles de la Fila 0 mapean en las direcciones
     *   DDRAM 0x00 a 0x0F (con el bit 7 en 1: 0x80 + col -> rango 0x80 a 0x8F).
     * - Las 16 celdas visibles de la Fila 1 mapean en las direcciones DDRAM 0x40 a 0x4F
     *   (con el bit 7 en 1: 0xC0 + col -> rango 0xC0 a 0xCF).
     */
    uint8_t address = (row == LCD_ROW_0) ? (CMD_SET_DDRAM_ROW0 + col) : (CMD_SET_DDRAM_ROW1 + col);
    // Envía el byte de posicionamiento al Registro de Instrucciones (RS = 0)
    send_command(address);
}

/**
 * @brief Escribe un único carácter ASCII en la posición actual del cursor.
 * @details Transmite el valor del carácter configurando la línea de selección de registro como dato.
 * @param[in] ch Carácter alfanumérico a visualizar.
 */
void Display16x2I2cWriteChar(char ch) {
    // Convierte el carácter a uint8_t y lo transmite con is_data = true (RS = 1, Registro de Datos DDRAM)
    send_byte((uint8_t)ch, true);
}

/**
 * @brief Escribe una cadena completa de caracteres terminada en carácter nulo.
 * @details Recorre secuencialmente la cadena de texto invocando la función de escritura de caracteres 
 *          hasta alcanzar el fin de cadena.
 * @param[in] str Puntero a la cadena de caracteres a imprimir.
 */
void Display16x2I2cWriteString(const char *str) {
    // Validación contra puntero nulo para prevenir accesos inválidos a memoria
    if (str == NULL) return;
    // Recorre byte a byte la cadena hasta hallar el delimitador nulo ('\0' = 0x00)
    while (*str != '\0') {
        Display16x2I2cWriteChar(*str); // Envía el carácter apuntado actualmente (el HD44780 avanza el cursor solo)
        str++;                         // Incrementa el puntero al siguiente carácter en memoria
    }
}

/**
 * @brief Escribe una línea completa de texto en una fila específica, rellenando el resto con espacios.
 * @details Posiciona el cursor al inicio de la fila indicada (\c 0x80 para Fila 0 o \c 0xC0 para Fila 1),
 *          imprime los caracteres de la cadena hasta completar el ancho de la pantalla (16 columnas) o encontrar
 *          el fin de cadena, y completa las celdas restantes con espacios (\c 0x20), produciendo un total de
 *          17 bytes (68 tramas I2C) por cada fila actualizada.
 * @param[in] row Fila de destino de la pantalla.
 * @param[in] str Puntero a la cadena de caracteres a mostrar en la línea.
 */
void Display16x2I2cWriteLine(uint8_t row, const char *str) {
    // 1. Envía el comando de posicionamiento al extremo izquierdo de la fila (columna 0) -> 1 byte = 4 tramas I2C
    Display16x2I2cSetCursor(row, 0);
    uint8_t i = 0;

    // 2. Transmite los caracteres válidos de la cadena sin exceder las 16 columnas físicas de la fila
    if (str != NULL) {
        while (str[i] != '\0' && i < LCD_COLS) {
            Display16x2I2cWriteChar(str[i]);
            i++;
        }
    }

    // 3. Si la cadena tenía menos de 16 caracteres, rellena las columnas restantes con espacios (' ' = 0x20)
    //    para sobrescribir y limpiar cualquier carácter perteneciente a un mensaje anterior más largo
    while (i < LCD_COLS) {
        Display16x2I2cWriteChar(' ');
        i++;
    }
}

/**
 * @brief Controla el estado de encendido o apagado de la luz de fondo del display.
 * @details Actualiza la variable de estado interna y transmite el patrón correspondiente al expansor I2C.
 * @param[in] state Estado deseado: \c true para encender la retroiluminación, \c false para apagarla.
 */
void Display16x2I2cSetBacklight(bool state) {
    // Actualiza la bandera estática para que las futuras llamadas a send_nibble() conserven este estado
    backlight_state = state;
    // Selecciona la máscara 0x08 (P3=1, resto de pines en 0) si se enciende, o 0x00 si se apaga
    uint8_t data = backlight_state ? MASK_BL : 0x00;
    // Aplica inmediatamente el cambio en las salidas físicas del expansor PCF8574
    pcf8574_write(data);
}


/**
 * @fn void Display16x2I2cTestPulseTrain(void)
 * @brief Ejecuta una secuencia de prueba barriendo los 8 bits de salida del expansor PCF8574 en la dirección 0x27.
 * @details Esta rutina genera un tren de pulsos de bit desplazado (*walking one*) orientado a la verificación física y
 *          experimental del mapeo entre los bits transmitidos por el bus I2C y los terminales del módulo 1602A Ver5.5
 *          (documentada en \c 00_conversor_I2C_para_display_16x2.txt, \c 01_i2c_01.png a \c 01_i2c_06.png y en las
 *          8 capturas de [01_i2c_todos_los_pulsos_de_prueba.jpg](../01_i2c_todos_los_pulsos_de_prueba.jpg)).
 *          
 *          Mediante un bucle \c for de 8 iteraciones espaciadas por \c vTaskDelay(pdMS_TO_TICKS(500)), invoca a
 *          \c pcf8574_write() con cada potencia de 2 (\c 1 << i), produciendo en el bus I2C las siguientes 8 tramas
 *          de 27 bits:
 *          - **Iteración 0 (P0 - HEX: 0x01):** Trama I2C \c [0x4E][0x01][0x01] (binario \c 00000001) -> Pin 4 del display se pone en ALTO (\c RS).
 *          - **Iteración 1 (P1 - HEX: 0x02):** Trama I2C \c [0x4E][0x02][0x02] (binario \c 00000010) -> Pin 5 del display se pone en ALTO (\c RW).
 *          - **Iteración 2 (P2 - HEX: 0x04):** Trama I2C \c [0x4E][0x04][0x04] (binario \c 00000100) -> Pin 6 del display se pone en ALTO (\c EN).
 *          - **Iteración 3 (P3 - HEX: 0x08):** Trama I2C \c [0x4E][0x08][0x08] (binario \c 00001000) -> Pin 16 (\c BLK) del display se conmuta a BAJO (0V), encendiendo el Backlight.
 *          - **Iteración 4 (P4 - HEX: 0x10):** Trama I2C \c [0x4E][0x10][0x10] (binario \c 00010000) -> Pin 11 del display se pone en ALTO (\c D4).
 *          - **Iteración 5 (P5 - HEX: 0x20):** Trama I2C \c [0x4E][0x20][0x20] (binario \c 00100000) -> Pin 12 del display se pone en ALTO (\c D5).
 *          - **Iteración 6 (P6 - HEX: 0x40):** Trama I2C \c [0x4E][0x40][0x40] (binario \c 01000000) -> Pin 13 del display se pone en ALTO (\c D6).
 *          - **Iteración 7 (P7 - HEX: 0x80):** Trama I2C \c [0x4E][0x80][0x80] (binario \c 10000000) -> Pin 14 del display se pone en ALTO (\c D7).
 */
void Display16x2I2cTestPulseTrain(void) {
    // Tabla de máscaras de un único bit activo (walking one) para excitar individualmente cada salida P0..P7
    const uint8_t pulse_sequence[] = {
        0x01, /* Bit 0 (00000001b) -> Salida P0: Pin 4 del display en ALTO (RS) */
        0x02, /* Bit 1 (00000010b) -> Salida P1: Pin 5 del display en ALTO (RW) */
        0x04, /* Bit 2 (00000100b) -> Salida P2: Pin 6 del display en ALTO (EN) */
        0x08, /* Bit 3 (00001000b) -> Salida P3: Pin 16 del display en BAJO (Backlight encendido) */
        0x10, /* Bit 4 (00010000b) -> Salida P4: Pin 11 del display en ALTO (D4) */
        0x20, /* Bit 5 (00100000b) -> Salida P5: Pin 12 del display en ALTO (D5) */
        0x40, /* Bit 6 (01000000b) -> Salida P6: Pin 13 del display en ALTO (D6) */
        0x80  /* Bit 7 (10000000b) -> Salida P7: Pin 14 del display en ALTO (D7) */
    };

    // Recorre las 8 posiciones enviando la trama I2C y manteniendo cada estado durante 500 ms
    for (uint8_t i = 0; i < 8; i++) {
        pcf8574_write(pulse_sequence[i]);
        vTaskDelay(pdMS_TO_TICKS(500)); /* Retardo no bloqueante de 500 ms para observación en osciloscopio */
    }
}

/*==================[end of file]============================================*/