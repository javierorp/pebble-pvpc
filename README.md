# pebble-pvpc

App de Pebble para consultar el precio de la luz (PVPC) en España, escrita en C
con el SDK de Pebble. Todo está en español.

## Qué muestra

- Una lista con las **24 horas del día** y su precio (formato español, con coma).
- Si REE ya ha publicado los precios del día siguiente (suele ocurrir entre las
  20:00 y las 21:00), se añaden esas filas con el prefijo **"Mañana"**.
- La fila de la hora en vigor está marcada con **→** y centrada automáticamente.
- Cada fila se colorea por terciles del día: **verde** las más baratas,
  **naranja** las intermedias y **rojo** las más caras (en relojes sin color:
  iconos **▼ / ◇ / ▲**).
- Precio en **€/kWh** o **€/MWh**, según configuración.

## Configuración de la app

Desde la configuración de la app se eligen:

- **Zona** (`geo_limit`): Península, Canarias, Baleares, Ceuta o Melilla.
- **Unidad del precio**: €/kWh o €/MWh.

En el emulador se abre con:

```sh
pebble emu-app-config --emulator emery
```

En un móvil real, con el icono de engranaje de la app dentro de la aplicación
de Pebble (Rebble).

## API

Los precios se obtienen del endpoint público de **Red Eléctrica** (apidatos):

```
https://apidatos.ree.es/es/datos/mercados/precios-mercados-tiempo-real
    ?start_date=AAAA-MM-DDT00:00
    &end_date=AAAA-MM-DDT23:59
    &time_trunc=hour
    &geo_limit=<zona>
```

Se hacen **dos peticiones** (hoy y mañana) y se fusionan los resultados.

Notas:

- `geo_limit` admite: `peninsular`, `canarias`, `baleares`, `ceuta`, `melilla`.
  Actualmente REE solo publica datos peninsulares en este endpoint.
- En Canarias la hora es una menos que en península: la app usa la hora
  canaria y desplaza la serie para que cada fila muestre el precio en vigor
  (la fila canaria "20:00 - 21:00" muestra el precio peninsular de las
  "21:00 - 22:00").

## Plataformas

| Plataforma | Dispositivo                    | Generación     | Resolución | Pantalla      |
|------------|--------------------------------|----------------|------------|---------------|
| aplite     | Pebble / Pebble Steel          | Pebble clásico | 144×168    | b/n           |
| basalt     | Pebble Time                    | Pebble clásico | 144×168    | color         |
| chalk      | Pebble Time Round              | Pebble clásico | 180×180    | color redonda |
| diorite    | Pebble 2 / Pebble 2 SE         | Pebble clásico | 144×168    | b/n           |
| flint      | Core 2 Duo (Pebble 2 Duo)      | Core Devices   | 144×168    | b/n           |
| gabbro     | Core Round 2 (Pebble Round 2)  | Core Devices   | 260×260    | color redonda |
| emery      | Core Time 2 (Pebble Time 2)    | Core Devices   | 200×228    | color         |

Las plataformas b/n (aplite, diorite, flint) usan iconos ▼/◇/▲ en lugar de
colores y un icono de menú propio en blanco y negro.

## Compilar e instalar

```sh
pebble build                          # compila para todas las plataformas
pebble install --emulator emery       # instala en el emulador
pebble install --phone <ip>           # instala en un reloj real (misma red)
```

## Estructura del proyecto

```
src/c/pebble-pvpc.c    Interfaz del reloj (lista, categorías, iconos)
src/pkjs/index.js      PebbleKit JS: peticiones a la API y página de ajustes
resources/images/      Icono de menú (color y b/n) e icono para la tienda
package.json           Metadatos, claves AppMessage y recursos
wscript                Reglas de compilación
```

## Notas del entorno de desarrollo

El SDK 4.33.1 necesita dos parches locales para el emulador (no afectan al
reloj real):

1. `libpebble2/communication/transports/qemu/__init__.py`: resincronización del
   stream QEMU (un paquete de vibración truncado bloquea el puente JS↔reloj).
2. `generate_appinfo.py`: filtrado de recursos por `targetPlatforms` para poder
   usar un icono de menú distinto en plataformas b/n y en color.

## Documentación

Documentación completa del SDK: <https://developer.repebble.com>
