# wattcycle-bms-parser

Mit diesem Arduino-Projekt ist es möglich das BMS einer LiFePo4 WattCycle 48V 100A Rack-Batterie auslesen und, die Werte per MQTT an iobroker zu senden.


> [!WARNING]
> Ich übernehme keinerlei Haftung für Schäden an Personen oder Hardware die durch dieses Projekt entstehen. Arbeiten an Spannungen größer 24V sollten nur von Fachpersonal durchgeführt werden!  
 







## Schaltung
### Bauteile
- ESP32-S3 DevKitC-1
- RS485 Entwicklungsboard TTL zu RS485, MAX485

Hinweis: Das RS485 Entwicklungsboard verwendet einen MAX485 Pegelwandler der für eine Versorgungsspannung von 5V ausgelegt ist. Da die GPIO's des ESP32 dauerhaft nur 3.3V vertragen wird die Spannung Vcc vom RS485 Entwicklungsboard am 3.3V Ausgang des ESP32 abgegriffen. Das RS485 Etwicklungsboard arbeitet auch zuverlässig mit 3.3V. Die 5V Spannungsversorgung des ESP32 kann entweder über USB oder den Anschlus-Pin VIN erfolgen.


### Bild 1: Schaltung
<img src="https://github.com/matlen67/wattcycle-bms-parser/blob/main/image/Schaltplan.png" width="512">

### Bild 2: mqtt iobroker
<img src="https://github.com/matlen67/wattcycle-bms-parser/blob/main/image/mqtt_iobroker.png" width="512">
