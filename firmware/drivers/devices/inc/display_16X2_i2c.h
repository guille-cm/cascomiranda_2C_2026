#ifndef DISPLAY_16X2_I2C_H
#define DISPLAY_16X2_I2C_H
/**
 * @file display_16x2_i2c.h
 * @brief Archivo de cabecera para el controlador del display LCD 16x2 mediante bus I2C (expansor PCF8574).
 * 
 * @details Implementa la capa de abstracción de dispositivo para comandar una pantalla
 *          alfanumérica basada en el controlador HD44780 en modo de interfaz de 4 bits a través
 *          de un expansor de entradas/salidas digitales I2C PCF8574 en la dirección de 7 bits 0x27. @n
 * 
 * @section library_justification Justificación y Operación Integral de la Librería
 * La presente librería se justifica ante la complejidad inherente de comandar un display 
 * alfanumérico estándar (basado en el controlador HD44780) mediante un microcontrolador ESP32-C6. 
 * Dado que el controlador del display requiere múltiples líneas de control y un bus de datos 
 * paralelo que consumiría una gran cantidad de pines de propósito general (GPIO), la arquitectura 
 * integra un expansor I2C (PCF8574) que reduce la interfaz física a tan solo dos líneas (SDA y SCL).
 * 
 * El flujo operacional que justifica el desarrollo de este driver comprende las siguientes etapas:
 * - <b>Inicialización y Configuración de Hardware:</b> Se establece la comunicación serie I2C y se ejecuta 
 *   una secuencia estricta de comandos y retardos temporales para forzar al controlador HD44780 a 
 *   operar en modo de 4 bits, optimizando el ancho de banda y sincronizando el dispositivo.
 * - <b>Multiplexación y Envío de Comandos/Datos:</b> Cada byte de información se desglosa internamente 
 *   en dos nibbles (alto y bajo), los cuales se combinan con las señales de control (como el registro RS 
 *   y el pulso de habilitación EN) antes de ser transmitidos a través del expansor I2C.
 * - <b>Abstracción de Alto Nivel:</b> Se proveen funciones dedicadas al posicionamiento del cursor, 
 *   limpieza de pantalla, encendido de retroiluminación y escritura de cadenas de caracteres, 
 *   aislando a la capa de aplicación de los detalles de bajo nivel del protocolo y simplificando su
 *   integración en aplicaciones concurrentes bajo FreeRTOS.
 * 
 * @section i2c_comm_strategy Fundamentos de Comunicación I2C, Estrategia de Manipulación de Datos e Integración en FreeRTOS
 * 
 * ### 1. Fundamentos Teóricos y Físicos del Bus I2C y del Expansor PCF8574
 * El protocolo <b>I2C (<i>Inter-Integrated Circuit</i>)</b> es un bus de comunicación serie sincrónico,
 * semidúplex (<i>half-duplex</i>) y multi-maestro/multi-esclavo que opera sobre dos líneas físicas con
 * etapa de salida a drenador abierto (<i>open-drain</i>):
 * - <b>SDA (<i>Serial Data</i>, GPIO_6):</b> Línea bidireccional por la que se transfieren las direcciones,
 *   los bits de datos (enviados desde el bit más significativo MSB al menos significativo LSB) y los
 *   pulsos de reconocimiento (<i>ACK</i>).
 * - <b>SCL (<i>Serial Clock</i>, GPIO_7):</b> Línea de reloj generada exclusivamente por el dispositivo maestro
 *   (ESP32-C6) a una frecuencia estándar de <b>100 kHz</b> (<b>T<sub>SCL</sub> = 10 µs</b> por bit).
 * 
 * Debido a que el controlador HD44780 del display <b>1602A Ver5.5</b> opera con niveles lógicos y de
 * contraste de <b>+5V DC</b> mientras que el ESP32-C6 utiliza lógica nativa de <b>+3.3V DC</b>, entre ambos
 * dominios de tensión se intercala un conversor de nivel lógico bidireccional basado en transistores
 * MOSFET canal N con resistencias <i>pull-up</i> a 3.3V en el lado de baja tensión (\c LV) y a 5.0V en el
 * lado de alta tensión (\c HV). Asimismo, sobre los bornes de alimentación de +5V y GND del módulo
 * display se dispone de desacople capacitivo local (<b>100 µF</b> electrolítico en paralelo con
 * <b>100 nF</b> cerámico) para garantizar la estabilidad de tensión y evitar la corrupción de bits
 * en los registros internos del PCF8574 durante transitorios de corriente en la línea de +5V.
 * 
 * Cada transacción física en el bus I2C se compone de:
 * 1. <b>Condición de Inicio (<i>START</i>):</b> Transición de alto a bajo en \c SDA mientras \c SCL permanece en nivel alto.
 * 2. <b>Byte de Direccionamiento Esclavo (\c 0x4E):</b> El circuito integrado PCF8574 tiene sus pines de
 *    dirección física \c A2=1, \c A1=1, \c A0=1, fijando la dirección de 7 bits \c 0x27 (\c 0100111 en binario).
 *    Para escribir en el dispositivo, el periférico I2C del microcontrolador desplaza esta dirección
 *    una posición hacia la izquierda (\c 0x27 << 1 = \c 0x4E) e inserta en el bit menos significativo (bit 0)
 *    el indicador \c R/W=0 (escritura), conformando el byte \c 01001110 (\c 0x4E).
 * 3. <b>Pulso de Reconocimiento (<i>ACK</i>):</b> Durante el 9.° pulso de reloj \c SCL, el maestro libera la línea
 *    \c SDA y el expansor PCF8574 la fuerza a nivel bajo (\c 0V), confirmando su presencia en el bus.
 * 4. <b>Transferencia de Bytes hacia el Puerto cuasi-bidireccional (\c P7..P0):</b> El PCF8574 carece de
 *    registros de configuración o direcciones de memoria interna. Cada byte completo de 8 bits recibido
 *    tras el byte de dirección es volcado de inmediato a sus 8 pines físicos de salida \c P7..P0 en el
 *    flanco del pulso de \c ACK.
 * 5. <b>Condición de Parada (<i>STOP</i>):</b> Transición de bajo a alto en \c SDA mientras \c SCL se encuentra en nivel alto.
 * 
 * ### 2. Estrategia de Manipulación de Datos y Justificación de Funciones y Variables
 * El controlador gráfico alfanumérico <b>HD44780</b> posee dos registros internos fundamentales seleccionados
 * mediante el pin físico <b>RS (<i>Register Select</i>)</b>: el Registro de Instrucciones (\c RS=0, para comandos
 * como limpiar pantalla, configurar interfaz o posicionar la dirección de memoria \c DDRAM) y el Registro de
 * Datos (\c RS=1, donde se escriben los códigos ASCII de los caracteres a representar en la matriz de
 * cristal líquido). A su vez, el HD44780 requiere un pulso estroboscópico en su pin <b>E (<i>Enable</i>)</b>,
 * capturando el estado del bus de datos exactamente en el <b>flanco descendente</b> (\c 1 -> \c 0) de dicha señal.
 * 
 * Como el PCF8574 dispone únicamente de 8 pines de salida en total (\c P7..P0) y se deben gobernar tanto
 * las señales de control (\c RS, \c RW, \c E y el transistor de retroiluminación \c BL) como el bus de datos,
 * resulta físicamente imposible utilizar la interfaz paralela de 8 bits (\c D7..D0) del display. Por ello,
 * la estrategia de diseño configura al controlador HD44780 en <b>Modo de Interfaz de 4 bits</b>:
 * - Los 4 bits superiores del expansor (<b>\c P7..P4</b>) se conectan físicamente a las líneas altas del bus de
 *   datos del display (<b>\c D7..D4</b>, pines 14 a 11).
 * - Los 4 bits inferiores del expansor (<b>\c P3..P0</b>) se asignan a las líneas de control: <b>\c P3 = BL</b>
 *   (\c MASK_BL = \c 0x08), <b>\c P2 = EN</b> (\c MASK_EN = \c 0x04), <b>\c P1 = RW</b> (\c MASK_RW = \c 0x02, mantenido en \c 0
 *   lógico permanente para escritura) y <b>\c P0 = RS</b> (\c MASK_RS = \c 0x01).
 * 
 * A partir de esta arquitectura física surge la necesidad de las variables y funciones implementadas en
 * \c display_16x2_i2c.c:
 * - <b>Variable estática \c backlight_state:</b> Dado que el PCF8574 sobrescribe la totalidad de sus 8 pines
 *   (\c P7..P0) en cada transacción I2C, si no se conservara en memoria el estado deseado de la luz de fondo,
 *   cualquier escritura de un carácter apagaría el pin \c P3. La variable \c backlight_state retiene este estado
 *   para inyectar la máscara \c MASK_BL en cada cuarteto transmitido.
 * - <b>Función interna \c pcf8574_write(uint8_t data):</b> Encapsula la invocación a la primitiva oficial de la
 *   cátedra \c I2C_writeByte(LCD_I2C_ADDR, data, data). Como dicha primitiva transmite dos bytes sucesivos
 *   tras la dirección del esclavo (\c regAddr y \c data), pasar el mismo byte en ambos argumentos asegura que
 *   los pines \c P7..P0 adopten el valor deseado en el primer byte y lo mantengan inalterado en el segundo,
 *   regulando naturalmente el ancho del pulso \c Enable en ~270 µs gracias a la velocidad del bus de 100 kHz.
 * - <b>Función interna \c send_nibble(uint8_t nibble, bool is_data):</b> Toma un cuarteto de 4 bits (\c 0x00..0x0F),
 *   lo desplaza 4 posiciones a la izquierda (\c (nibble & 0x0F) << 4) para situarlo en \c P7..P4, aplica mediante
 *   operaciones OR bit a bit las máscaras \c MASK_RS (si \c is_data == true) y \c MASK_BL, y ejecuta dos llamadas
 *   consecutivas a \c pcf8574_write(): la primera con \c data | MASK_EN (elevando \c E a nivel alto) y la segunda
 *   con \c data & ~MASK_EN (forzando el flanco descendente en \c E para que el HD44780 latchee el cuarteto).
 * - <b>Función interna \c send_byte(uint8_t value, bool is_data):</b> Fragmenta un byte completo de 8 bits en dos
 *   envíos secuenciales: primero extrae y envía el cuarteto más significativo (\c value >> 4) y a continuación
 *   el cuarteto menos significativo (\c value & 0x0F), totalizando 4 tramas I2C por cada byte procesado.
 * - <b>Funciones públicas de la API (\c Display16x2I2cInit, \c Display16x2I2cWriteLine, etc.):</b> Proveen a la
 *   aplicación principal un conjunto de servicios de alto nivel orientados a filas completas de 16 caracteres,
 *   calculando internamente los desplazamientos de memoria \c DDRAM (\c 0x80 para Fila 0 y \c 0xC0 para Fila 1) y
 *   rellenando con espacios en blanco (\c 0x20) las celdas sobrantes para evitar residuos visuales en pantalla.
 * 
 * ### 3. Integración y Disponibilidad del Módulo dentro del Sistema Operativo FreeRTOS
 * Dentro de la arquitectura multitarea de \c monitor_unidad_paciente.c, este módulo es inicializado una única
 * vez en \c app_main() (previo al arranque del servidor Wi-Fi) y posteriormente es consumido de forma exclusiva
 * por la tarea <b>\c TaskDisplayLCD</b> (Prioridad 2, período base de ciclo de 200 ms y refresco general de 1000 ms):
 * - <b>Ausencia de bloqueos por espera activa (<i>Busy-Wait</i>):</b> Durante \c Display16x2I2cInit() y \c Display16x2I2cClear(),
 *   todos los tiempos de espera exigidos por la hoja de datos del controlador HD44780 (100 ms iniciales, 10 ms,
 *   5 ms y 2 ms) se implementan mediante la primitiva del sistema operativo <b>\c vTaskDelay(pdMS_TO_TICKS(...))</b>,
 *   transicionando la tarea al estado <b>Blocked</b> y cediendo el procesador RISC-V al resto de las tareas.
 *   Asimismo, durante el envío de nibbles no se emplean temporizadores de hardware (\c DelayUs), dado que el
 *   propio tiempo de transmisión de la trama I2C a 100 kHz (~270 µs) supera ampliamente el ancho mínimo de pulso
 *   \c Enable requerido por el HD44780 (>450 ns) y el tiempo de ejecución de instrucción (>37 µs). Esto evita
 *   cualquier colisión o condición de carrera sobre los temporizadores de hardware \c GPTimer.
 * - <b>Preempción transparente por tareas de Tiempo Real Estricto (500 Hz):</b> Dado que \c TaskDisplayLCD se ejecuta
 *   en <b>Prioridad 2</b>, durante los ~58 ms que dura la actualización completa de las 2 filas (136 tramas I2C),
 *   cada 2 ms (500 Hz) las interrupciones de los temporizadores de hardware despiertan mediante notificaciones
 *   directas (\c vTaskNotifyGiveFromISR) a las tareas críticas <b>\c TaskGenerarECG (Prioridad 5)</b> y
 *   <b>\c TaskLeerECG_UART (Prioridad 4)</b>. El planificador (<i>scheduler</i>) de FreeRTOS pasa a \c TaskDisplayLCD del
 *   estado <b>Running</b> al estado <b>Ready</b>, ejecuta la síntesis analógica y el muestreo de Teleplot en menos de
 *   150 µs, y devuelve a \c TaskDisplayLCD al estado <b>Running</b> sin afectar en absoluto la integridad de la trama
 *   I2C (fenómeno verificado experimentalmente en la Ventana 2 de la captura de osciloscopio).
 * 
 * @section op_summary Resumen Operacional del Driver I2C para Display LCD 16x2
 * El sistema establece una comunicación serie mediante el bus I2C desde el microcontrolador ESP32-C6 
 * hacia un módulo expansor de E/S basado en el circuito integrado PCF8574 (con dirección fija 0x27). 
 * Este expansor traduce los comandos y datos seriales en señales paralelas de 8 bits que gobiernan 
 * el controlador interno del display HD44780.
 * 
 * ### 1. Mapeo y Multiplexación de Pines a través del Expansor
 * El expansor PCF8574 recibe un byte de 8 bits (P0 a P7) por el bus I2C a 100 kHz y fija los voltajes 
 * en sus pines físicos de salida de la siguiente manera:
 * - <b>P0 (MASK_RS):</b> Pin de selección de registro (Register Select). Si está en bajo (0), el byte enviado 
 *   se procesa como un comando de control (ej. limpiar pantalla, mover cursor). Si está en alto (1), 
 *   se interpreta como un dato/carácter ASCII para visualizar.
 * - <b>P1 (MASK_RW):</b> Pin de lectura/escritura (Read/Write). Se mantiene permanentemente en nivel bajo (0) 
 *   ya que el ESP32 únicamente realiza operaciones de escritura en la pantalla.
 * - <b>P2 (MASK_EN):</b> Pin de habilitación (Enable). Se utiliza para generar un pulso (transición de nivel 
 *   alto a bajo) que le indica al controlador HD44780 que capture los datos presentes en sus pines de entrada.
 * - <b>P3 (MASK_BL):</b> Controla el circuito de encendido y apagado de la luz de fondo (Backlight).
 * - <b>P4 a P7 (D4 a D7):</b> Constituyen el bus de datos de 4 bits del LCD. Como el display opera en modo de 4 bits 
 *   para ahorrar pines del expansor, cada byte de información se divide y se transmite en dos partes 
 *   secuenciales (nibble alto y nibble bajo).
 * 
 * ### 2. Proceso de Transmisión Física y Protocolo
 * - <b>Función de Escritura Básica (pcf8574_write):</b> Al carecer el PCF8574 de registros internos direccionables, 
 *   se emplea la función del driver de la cátedra \c I2C_writeByte(LCD_I2C_ADDR, data, data) replicando el byte de datos 
 *   en ambos parámetros exigidos.
 * - <b>Envío por Nibbles (send_nibble y send_byte):</b> La función \c send_byte toma un byte de 8 bits, desplaza sus 4 bits 
 *   más significativos a la posición alta del puerto del expansor (P4-P7) y ejecuta un ciclo de habilitación activando 
 *   y desactivando el pin Enable (MASK_EN). Posteriormente, repite el proceso con los 4 bits menos significativos.
 * - <b>Inicialización y Comandos del HD44780:</b> Durante la rutina de arranque (\c Display16x2I2cInit), se envían comandos 
 *   específicos bajo una secuencia estricta de temporización con retardos gestionados en FreeRTOS para configurar 
 *   el controlador en modo de 4 bits, activar el display, limpiar la memoria DDRAM y fijar el modo de incremento del cursor.
 * 
 * @section validation Validación Experimental y Comportamiento del Expansor I2C
 * - <b>Video de prueba en YouTube:</b> @n
 *   [Conexión I2C ESP32, conversor de nivel, conversor I2C, y display LCD 16x2](https://youtube.com/shorts/58nJPL-9mYg) @n
 *   El kit de desarrollo de este ESP32 funciona con niveles lógicos de 3.3 V. Para un funcionamiento adecuado en brillo, contraste y comunicación con el display LCD de 16x2 típico, se interpone un conversor de nivel lógico bidireccional entre el ESP32 y el conversor I2C-display. @n
 * 
 * @see [Registro de Prueba del Conversor I2C para Display 16x2](../01_conversor_I2C_para_display_16x2.txt)
 * @see [Pinout del Conversor I2C PCF8574T](../i2c_conversor_I2C_PCF8574T_pinout.png)
 * @see [Tren de Pulsos de Prueba del Bus I2C en Osciloscopio](../i2c_todos_los_pulsos_de_prueba.png)
 * @see [Módulo Display LCD 16x2 y Expansor I2C](../display_y_expansor_i2c.jpg)
 * 
 * En dichas capturas se comprueba visualmente la aplicación del protocolo I2C: tras la condición de inicio (<i>START</i>), 
 * se transmite el byte de dirección del esclavo (\c 0x27, visualizado en el bus como \c 0x4E) acompañado del bit de
 * reconocimiento (<i>ACK</i>), seguido de un tren de pulsos que recorre los 8 bits de datos para verificar el estado de
 * los canales de salida del expansor en el siguiente orden:
 * - <b>P0 (HEX: 0x01):</b> El pin  4 del expansor I2C se pone en ALTO (función RS).
 * - <b>P1 (HEX: 0x02):</b> El pin  5 del expansor I2C se pone en ALTO (función RW).
 * - <b>P2 (HEX: 0x04):</b> El pin  6 del expansor I2C se pone en ALTO (función EN).
 * - <b>P3 (HEX: 0x08):</b> El pin 16 del expansor I2C se pone en BAJO (retroiluminación / backlight).
 * - <b>P4 (HEX: 0x10):</b> El pin 11 del expansor I2C se pone en ALTO (función D4).
 * - <b>P5 (HEX: 0x20):</b> El pin 12 del expansor I2C se pone en ALTO (función D5).
 * - <b>P6 (HEX: 0x40):</b> El pin 13 del expansor I2C se pone en ALTO (función D6).
 * - <b>P7 (HEX: 0x80):</b> El pin 14 del expansor I2C se pone en ALTO (función D7).
 * 
 * Esta experimentación permite implementar de manera determinística el protocolo I2C para el manejo del display
 * LCD de 16x2 comercial.
 * 
 * @section scope_analysis_07 Análisis de Osciloscopio del Tráfico de Pantalla
 * En la captura compuesta [i2c_todos_los_pulsos_de_prueba.png](../i2c_todos_los_pulsos_de_prueba.png)
 * se registra la medición en el lado de 5V del conversor de nivel lógico bidireccional (Canal 1 amarillo: \c SDA;
 * Canal 2 verde: \c SCL) durante la actualización periódica del mensaje \c "BIENVENIDO!" (Fila 0) y
 * \c "FC: 60 - FR: 15 " (Fila 1):
 * 
 * ### 1. Análisis Temporal Macroscópico (Ventanas 1, 2 y 3)
 * - <b>Estructura de un refresco completo (2 filas):</b> Cada llamada a \c Display16x2I2cWriteLine() envía <b>17 bytes</b>
 *   hacia el controlador HD44780 (1 byte de comando de dirección DDRAM \c 0x80 o \c 0xC0 + 16 caracteres ASCII).
 *   Al operar en modo de 4 bits, cada byte se divide en 2 nibbles y cada nibble requiere 2 tramas I2C (\c EN=1 y \c EN=0),
 *   totalizando <b>4 tramas I2C por byte</b>, <b>68 tramas I2C por fila</b> y <b>136 tramas I2C por refresco completo de pantalla</b>.
 * - <b>Ventana 1 (Paquete completo de 2 filas):</b> Muestra la ráfaga completa de 136 tramas I2C con una duración activa
 *   total de <b>~58 a 60 ms</b>, precedida y seguida por ~940 ms de reposo (líneas en 5V) que completan el período de 1 s
 *   de \c TaskDisplayLCD. La breve pausa observable hacia el centro del bloque corresponde a la transición entre el fin
 *   de \c Display16x2I2cWriteLine(LCD_ROW_0, ...) y el inicio de \c Display16x2I2cWriteLine(LCD_ROW_1, ...) tras ejecutar \c snprintf().
 * - <b>Ventana 2 (Ráfagas e interrupciones del RTOS):</b> Al ampliar la escala horizontal se distinguen las separaciones de
 *   ~150 µs entre tramas (gestión de manejador en \c i2c_mcu.c) y las pausas regulares cada <b>2 ms (500 Hz)</b> producidas cuando
 *   las interrupciones de los Timers A y B suspenden momentáneamente a \c TaskDisplayLCD (prioridad 2) para ejecutar las
 *   tareas de tiempo real \c TaskGenerarECG (prioridad 5) y \c TaskLeerECG_UART (prioridad 4).
 * - <b>Ventana 3 (Detalle de 1 byte completo = 4 tramas I2C):</b> Exhibe 4 tramas I2C consecutivas (más el inicio de una 5.ª).
 *   Cada trama dura <b>~270 µs</b> (27 pulsos de reloj \c SCL a 100 kHz). El conjunto de 4 tramas constituye el envío completo
 *   de un byte de 8 bits hacia el display.
 * 
 * ### 2. Decodificación a Nivel de Bit de las Ventanas 4, 5, 6 y 7
 * Las últimas 4 ventanas desglosan las 4 tramas I2C que transmiten el primer byte de cada ciclo de refresco: el comando
 * \c CMD_SET_DDRAM_ROW0 (\c 0x80 = \c 10000000 en binario), invocado por \c Display16x2I2cSetCursor(LCD_ROW_0, 0).
 * En cada ventana se envían exactamente <b>27 bits</b> (3 bytes de 8 bits + 3 bits \c ACK en nivel bajo \c 0) entre las
 * condiciones de \c Start y \c Stop:
 * | Ventana | Trama Binaria en SDA (27 bits) | Hexadecimal | Significado Funcional en el PCF8574 y HD44780 |
 * | :------ | :----------------------------- | :---------- | :-------------------------------------------- |
 * | <b>Ventana 4</b> | \c Start \c 01001110 + \c A(0) \c 10001100 + \c A(0) \c 10001100 + \c A(0) \c Stop | \c 0x4E, \c 0x8C, \c 0x8C | Dirección \c 0x27+W (\c 0x4E). Nibble alto de \c 0x80 (\c D7..D4=1000), \c BL=1 (\c P3=1), <b>\c EN=1 (\c P2=1)</b>, \c RW=0, \c RS=0 (Comando). |
 * | <b>Ventana 5</b> | \c Start \c 01001110 + \c A(0) \c 10001000 + \c A(0) \c 10001000 + \c A(0) \c Stop | \c 0x4E, \c 0x88, \c 0x88 | Dirección \c 0x27+W (\c 0x4E). Mantiene \c D7..D4=1000 y \c BL=1, y conmuta <b>\c EN=0 (\c P2=0)</b>: el flanco descendente captura el nibble alto \c 1000. |
 * | <b>Ventana 6</b> | \c Start \c 01001110 + \c A(0) \c 00001100 + \c A(0) \c 00001100 + \c A(0) \c Stop | \c 0x4E, \c 0x0C, \c 0x0C | Dirección \c 0x27+W (\c 0x4E). Nibble bajo de \c 0x80 (\c D7..D4=0000), \c BL=1 (\c P3=1), <b>\c EN=1 (\c P2=1)</b>, \c RW=0, \c RS=0 (Comando). |
 * | <b>Ventana 7</b> | \c Start \c 01001110 + \c A(0) \c 00001000 + \c A(0) \c 00001000 + \c A(0) \c Stop | \c 0x4E, \c 0x08, \c 0x08 | Dirección \c 0x27+W (\c 0x4E). Mantiene \c D7..D4=0000 y \c BL=1, y conmuta <b>\c EN=0 (\c P2=0)</b>: captura el nibble bajo \c 0000 y completa \c 0x80. |
 * 
 * @section hardConn Hardware Connection
 * 
 * ### Conexión entre ESP32-C6, Conversor de Nivel Lógico Bidireccional y Módulo I2C (PCF8574):
 * | Señal / Dispositivo | Pin ESP32-C6 | Lado Baja Tensión (3.3V) | Lado Alta Tensión (5V) | Pin Módulo I2C (PCF8574) |
 * | :------------------ | :----------- | :----------------------- | :--------------------- | :----------------------- |
 * | Datos I2C (SDA)     | GPIO_6       | TX1LCH1                  | TX0HCH1                | SDA                      |
 * | Reloj I2C (SCL)     | GPIO_7       | TX1LCH2                  | TX0HCH2                | SCL                      |
 * | Alimentación 3.3V   | 3V3          | LV                       | -                      | -                        |
 * | Alimentación 5V     | 5V           | -                        | HV                     | VCC (con 100 µF + 100 nF)|
 * | Masa común          | GND          | GND (puenteado)          | GND (puenteado)        | GND                      |
 * 
 * ### Mapeo Interno entre PCF8574 y Display LCD 16x2 (HD44780):
 * | Pin PCF8574 | Pin LCD 16x2 | Señal LCD | Función |
 * | :---------- | :----------- | :-------- | :------ |
 * | P0          | Pin 4        | RS        | Selección de Registro (0: Comando, 1: Dato) |
 * | P1          | Pin 5        | R/W       | Lectura/Escritura (Forzado a 0 para escritura) |
 * | P2          | Pin 6        | E (EN)    | Pulso de habilitación (Latch en flanco descendente) |
 * | P3          | Pin 16       | BL        | Control de luz de fondo (Backlight) |
 * | P4          | Pin 11       | D4        | Bit 4 del bus de datos (Modo 4 bits) |
 * | P5          | Pin 12       | D5        | Bit 5 del bus de datos (Modo 4 bits) |
 * | P6          | Pin 13       | D6        | Bit 6 del bus de datos (Modo 4 bits) |
 * | P7          | Pin 14       | D7        | Bit 7 del bus de datos (Modo 4 bits) |
 * 
 * ### Disposición Completa de los 16 Pines del Módulo Display LCD 16x2 (Modelo 1602A Ver5.5):
 * | Pin Display | Denominación en Serigrafía | Estado / Conexión en Modo 4 Bits | Función Eléctrica y Operativa |
 * | :---------- | :------------------------- | :------------------------------- | :---------------------------- |
 * | <b>Pin 1</b>   | \c GND (\c VSS)         | Conectado a \c GND               | Referencia de masa lógica (0V). |
 * | <b>Pin 2</b>   | \c VDD (\c VCC)         | Conectado a \c +5V               | Alimentación positiva del controlador HD44780 (+5V). |
 * | <b>Pin 3</b>   | \c VO (\c VEE)          | Salida de preset del módulo I2C  | Tensión de polarización para ajuste de contraste del cristal líquido. |
 * | <b>Pin 4</b>   | \c RS                   | Conectado a \c P0 del PCF8574    | Selección de registro (\c 0: Instrucción/Comando, \c 1: Dato ASCII). |
 * | <b>Pin 5</b>   | \c RW                   | Conectado a \c P1 del PCF8574    | Lectura/Escritura (Fijado en \c 0 lógico para operación de escritura). |
 * | <b>Pin 6</b>   | \c E                    | Conectado a \c P2 del PCF8574    | Habilitación (\c Enable). Captura el nibble en el flanco descendente (\c 1 -> \c 0). |
 * | <b>Pin 7</b>   | \c D0                   | Sin conexión en modo 4 bits      | Bit 0 del bus de datos paralelo (usado solo en interfaz de 8 bits). |
 * | <b>Pin 8</b>   | \c D1                   | Sin conexión en modo 4 bits      | Bit 1 del bus de datos paralelo (usado solo en interfaz de 8 bits). |
 * | <b>Pin 9</b>   | \c D2                   | Sin conexión en modo 4 bits      | Bit 2 del bus de datos paralelo (usado solo en interfaz de 8 bits). |
 * | <b>Pin 10</b>  | \c D3                   | Sin conexión en modo 4 bits      | Bit 3 del bus de datos paralelo (usado solo en interfaz de 8 bits). |
 * | <b>Pin 11</b>  | \c D4                   | Conectado a \c P4 del PCF8574    | Bit 4 del bus de datos (recibe bit 4 en nibble alto y bit 0 en nibble bajo). |
 * | <b>Pin 12</b>  | \c D5                   | Conectado a \c P5 del PCF8574    | Bit 5 del bus de datos (recibe bit 5 en nibble alto y bit 1 en nibble bajo). |
 * | <b>Pin 13</b>  | \c D6                   | Conectado a \c P6 del PCF8574    | Bit 6 del bus de datos (recibe bit 6 en nibble alto y bit 2 en nibble bajo). |
 * | <b>Pin 14</b>  | \c D7                   | Conectado a \c P7 del PCF8574    | Bit 7 del bus de datos (recibe bit 7 en nibble alto y bit 3 en nibble bajo). |
 * | <b>Pin 15</b>  | \c BLA (\c A)           | Alimentado a +5V vía jumper I2C  | Ánodo del LED de retroiluminación (Backlight Anode). |
 * | <b>Pin 16</b>  | \c BLK (\c K)           | Conmutado por transistor en \c P3| Cátodo del LED de retroiluminación (Backlight Cathode, puesto a 0V cuando \c P3=1). |
 * 
 * @section changelog Changelog
 * | Date       | Description |
 * | :--------- | :---------- |
 * | 2026-10-08 | Creación del documento empleando el driver i2c_mcu |
 * 
 * @version 1.0
 * @author Guillermo Casco Miranda
 */

/*==================[inclusions]=============================================*/
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_mcu.h"    /*!< Incluye el driver I2C oficial de la cátedra */

/*==================[macros and definitions]=================================*/
#define LCD_I2C_ADDR        0x27    /*!< Dirección I2C del expansor PCF8574 */
#define LCD_COLS            16      /*!< Cantidad de columnas del display */
#define LCD_ROWS            2       /*!< Cantidad de filas del display */
#define LCD_ROW_0           0       /*!< Identificador de la fila superior */
#define LCD_ROW_1           1       /*!< Identificador de la fila inferior */

/*==================[typedef]================================================*/

/*==================[external data declaration]==============================*/

/*==================[external function declarations]=========================*/
/**
 * @fn bool Display16x2I2cInit(void)
 * @brief Inicializa el periférico I2C a 100 kHz y ejecuta la secuencia de reseteo por instrucción
 *        para configurar el controlador HD44780 en interfaz de 4 bits, 2 líneas y matriz de 5x8 puntos.
 * @return true al completarse exitosamente la secuencia de inicialización.
 */
bool Display16x2I2cInit(void);

/**
 * @fn void Display16x2I2cClear(void)
 * @brief Borra la totalidad de la memoria DDRAM de la pantalla (comando 0x01), retorna el cursor
 *        a la coordenada (0,0) y cede la CPU durante 5 ms en estado Blocked hasta completar la operación.
 */
void Display16x2I2cClear(void);

/**
 * @fn void Display16x2I2cSetCursor(uint8_t row, uint8_t col)
 * @brief Posiciona el puntero de dirección DDRAM del controlador HD44780 en la fila y columna indicadas.
 * @param[in] row Fila de destino (\c LCD_ROW_0 = 0 para fila superior, \c LCD_ROW_1 = 1 para fila inferior).
 * @param[in] col Columna de destino (acotada automáticamente al rango válido de 0 a 15).
 */
void Display16x2I2cSetCursor(uint8_t row, uint8_t col);

/**
 * @fn void Display16x2I2cWriteChar(char ch)
 * @brief Transmite un único carácter ASCII al Registro de Datos (\c RS=1) en la posición actual del cursor.
 * @param[in] ch Carácter ASCII de 8 bits a imprimir (se divide internamente en dos nibbles de 4 bits).
 */
void Display16x2I2cWriteChar(char ch);

/**
 * @fn void Display16x2I2cWriteString(const char *str)
 * @brief Imprime secuencialmente una cadena de caracteres terminada en nulo ('\0') desde la posición actual.
 * @param[in] str Puntero constante a la cadena de texto a visualizar.
 */
void Display16x2I2cWriteString(const char *str);

/**
 * @fn void Display16x2I2cWriteLine(uint8_t row, const char *str)
 * @brief Posiciona el cursor al inicio de la fila seleccionada, escribe hasta 16 caracteres de la cadena
 *        y rellena las celdas restantes con espacios (' ') para suprimir caracteres residuales previos.
 * @param[in] row Fila a actualizar (\c LCD_ROW_0 o \c LCD_ROW_1).
 * @param[in] str Puntero a la cadena de caracteres a mostrar en la fila.
 */
void Display16x2I2cWriteLine(uint8_t row, const char *str);

/**
 * @fn void Display16x2I2cSetBacklight(bool state)
 * @brief Actualiza la retención interna de retroiluminación y conmuta de inmediato el pin P3 del PCF8574.
 * @param[in] state \c true para activar la luz de fondo (\c P3=1), \c false para apagarla (\c P3=0).
 */
void Display16x2I2cSetBacklight(bool state);

/**
 * @fn void Display16x2I2cTestPulseTrain(void)
 * @brief Ejecuta una secuencia experimental de barrido de bit desplazado (*walking one*, \c 0x01 a \c 0x80)
 *        sobre las 8 salidas del expansor PCF8574 en la dirección 0x27 para verificación con osciloscopio.
 */
void Display16x2I2cTestPulseTrain(void);

#endif /* DISPLAY_16X2_I2C_H */
/*==================[end of file]============================================*/