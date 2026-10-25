# Práctica MQTT — Integración de un Objeto Inteligente con MQTT y MCP

**Carrera:** Ingeniería de Sistemas

**Docente:** Eduardo Enrique Marin Garcia

**Asignatura:** Internet de las Cosas [SIS-234]

**Semestre:** 2-2026

**Grupo:** 9

**Integrantes:**

- Carlos Eduardo Iriarte Rodriguez
- Isabella Catherine Montes Iñiguez
- Henry Alejandro Rivera Mendez

## Descripción

Proyecto PlatformIO para ESP32 DevKit V1, sensor HC-SR04 y servo convencional.

## Configuración

Edita **include/config.h**: Wi-Fi, host/puerto MQTT, credenciales opcionales,
prefijo de cliente, tópicos, intervalos, pines y calibración del servo/sensor.
Luego compila y carga el firmware. No hay que modificar main.cpp.
El servidor inicial es test.mosquitto.org, puerto 1883, anónimo y sin TLS:
https://test.mosquitto.org/
Usa tópicos únicos: cualquier usuario del servidor público puede leer o publicar.
La MAC se agrega al identificador del cliente, no a los tópicos.

## Cableado de referencia

| Elemento | ESP32 |
| --- | --- |
| HC-SR04 TRIG | GPIO 5 |
| HC-SR04 ECHO | GPIO 18 mediante divisor de tensión |
| Servo señal | GPIO 19 |
| GND sensor y servo | GND común con ESP32 |

Alimenta el HC-SR04 con 5 V. Su ECHO es de 5 V: usa un divisor
(por ejemplo ECHO → 1 kΩ → GPIO 18 → 2 kΩ → GND) para obtener unos 3,3 V.
Alimenta el servo con una fuente adecuada para su corriente y tensión; comparte
GND con el ESP32. Ajusta SERVO_MIN_US y SERVO_MAX_US según el modelo para no
forzar los extremos mecánicos.

## Uso

```sh
pio run
pio run --target upload
pio device monitor
```

Cada 5 segundos se mide la distancia. Si hay conexión MQTT y lectura válida,
se publica un número en centímetros con dos decimales, por ejemplo `23.45`.
Si no hay eco o está fuera del rango configurado, no se publica. Las lecturas
sin conexión se descartan; no se guardan para envío posterior.

El tópico del servo acepta exclusivamente texto `0`, `45`, `90`, `135` o `180`,
sin comillas, espacios ni saltos de línea. Otros mensajes se ignoran.
El servo inicia en 90 grados. La reconexión conserva su último ángulo y vuelve
a enviar la suscripción. Se reintenta Wi-Fi cada 10 s y MQTT cada 5 s.
Los intervalos usan millis(); la lectura tiene un timeout y la conexión MQTT
puede bloquear brevemente, por lo que la cadencia no es de tiempo real estricto.

Ejemplo con los tópicos iniciales (adáptalos si cambias la configuración):

```sh
mosquitto_sub -h test.mosquitto.org -t practica/cambiar-identificador/distancia
mosquitto_pub -h test.mosquitto.org -t practica/cambiar-identificador/servo -m 135
```

Prueba también valores inválidos y desconecta/reconecta el Wi-Fi o el broker.
Verifica en el monitor serie la recuperación y que se reciban nuevos comandos.

Dependencias: PubSubClient 2.8 y ESP32Servo 0.13.0 (Arduino ESP32 2.x).
API MQTT: https://pubsubclient.knolleary.net/api
