# Tarea 1 IoT BLE

Sistema BLE con un ESP32 como servidor GATT y una Raspberry Pi como cliente central. La Raspberry se conecta simultaneamente al ESP32 y a un telefono BLE, muestra los datos en tiempo real y permite guardar un CSV.

## Estructura

- `esp32/`: firmware ESP-IDF en C.
- `raspberry/`: cliente BLE e interfaz grafica en Python.

## ESP32

El ESP32 anuncia el nombre BLE:

```text
GATT BLE Server
```

Servicios y caracteristicas expuestas:

| Fuente | Servicio UUID | Caracteristica UUID | Propiedad |
| --- | --- | --- | --- |
| Acelerometro | `44332211-4433-2211-4433-221144332211` | `88776655-8877-6655-8877-665588776655` | Notify |
| Temperatura | `55443322-5544-3322-5544-332255443322` | `99887766-9988-7766-9988-776699887766` | Notify |

Formato binario de paquetes ESP32:

| Fuente | Formato little-endian | Bytes | Campos |
| --- | --- | --- | --- |
| Acelerometro | `<Ifff` | 16 | `timestamp_ms`, `ax`, `ay`, `az` |
| Temperatura | `<If` | 8 | `timestamp_ms`, `temp` |

`timestamp_ms` es generado por el ESP32 con `esp_log_timestamp()`, por lo que representa milisegundos desde el arranque del ESP32.

### Configuracion ESP-IDF

El proyecto se compila para ESP32:

```bash
cd ~/dev/iot/esp32
get-idf
idf.py set-target esp32
```

La configuracion Bluetooth usada es **solo NimBLE**. Si se regenera `sdkconfig`, verificar en `idf.py menuconfig`:

- Bluetooth habilitado.
- Host BLE: NimBLE.
- Bluedroid deshabilitado.
- Bluetooth Classic deshabilitado.

### Compilar y flashear

Conectar el ESP32 por USB. ESP-IDF detecta el puerto correcto automaticamente.

```bash
cd ~/dev/iot/esp32
get-idf
idf.py build
idf.py flash
```

Si aparece un error de chip incorrecto, limpiar y fijar nuevamente el target:

```bash
idf.py fullclean
idf.py set-target esp32
idf.py build
idf.py flash
```

## Telefono BLE

Configurar una app como LightBlue o nRF Connect para anunciar un periferico BLE con:

```json
{
  "name": "PhoneBLE",
  "char_uuid": "00002222-0000-1000-8000-00805f9b34fb"
}
```

La caracteristica puede ser `Read` o `Notify`. El cliente Raspberry interpreta el valor como texto UTF-8; si no se puede decodificar, lo muestra como hexadecimal.

## Raspberry Pi

Configuracion BLE en `raspberry/config.json`:

```json
{
  "esp32": {
    "name": "GATT BLE Server",
    "accel_char_uuid": "88776655-8877-6655-8877-665588776655",
    "temp_char_uuid": "99887766-9988-7766-9988-776699887766"
  },
  "phone": {
    "name": "PhoneBLE",
    "char_uuid": "00002222-0000-1000-8000-00805f9b34fb"
  },
  "ui": {
    "window_ms": 2000
  }
}
```

Instalar dependencias:

```bash
cd ~/dev/iot/raspberry
python3 -m pip install -r requirements.txt
```

Ejecutar la aplicacion:

```bash
cd ~/dev/iot
python3 raspberry/app.py
```

La interfaz muestra:

- Grafico en tiempo real de acelerometro `Ax`, `Ay`, `Az`.
- Ultima temperatura recibida con timestamp del ESP32.
- Ultimo valor recibido desde el telefono con timestamp de recepcion en la Raspberry.
- Estadisticas del acelerometro: RMS, peak positivo y pico a pico.
- Boton para iniciar/detener guardado CSV en `raspberry/data_log.csv`.
