
# DosingFeeder

Der DosingFeeder ist ein Controller für ein bis mehrere Peristaltikpumpen. Er besteht aus einem Wemos D1 mini und einem Relais-Breakoutboard pro Pumpe. Zur Spannungsversorgung kann die eingebaute USB-Buchse des Microcontrollers verwendet werden, wenn die Pumpen weniger als 500 mA Strom benötigen. Die Software stellt sicher, dass maximal eine Pumpe gleichzeitig läuft.

## Betrieb

Der µController läuft durchgehend und ist dauerhaft mit dem MQTT-Broker verbunden. Bei jeglicher Statusänderung wird eine entsprechende Meldung gesendet (publish). Sobald ein Pumpen-Auftrag ansteht (entweder spontan über den `shot`-Befehl oder zeitgesteuert), wird dieser Auftrag in eine Warteschlange eingereiht. Diese Warteschlange wird ständig überwacht und abgearbeitet, indem ein Auftrag nach dem anderen ausgeführt wird. Auf diese Weise wird jeweils immer nur eine Pumpe gleichzeitig eingeschaltet.

## Konfiguration

Die Basiskonfiguration läuft über WiFiManager, d.h. der Controller baut, wenn er nicht initialisiert ist, einen eigenen Access Point (ssid: "DosingFeeder-aabbcc" mit unterer Hälfte der MAC-Adresse als sechs Hex-Ziffern) auf; beim Verbinden damit wird eine Vorschaltseite angezeigt, in der das zu verwendende WiFi-Netzwerk mit Passwort konfiguriert wird, außerdem die Verbindung zum MQTT-Broker. Diese Parameter werden im EEPROM gespeichert und beim nächsten Systemstart verwendet. Um die Einstellungen zu löschen, muss zweimal innerhalb von drei Sekunden gestartet werden.

Der Controller verwendet für die Zeitsteuerung die aktuelle Uhrzeit. Dieser wird über NTP von einem Server bezogen und (fest) mit dem DE-Locale interpretiert, d.h. die Umstellung zwischen Normal- und Sommerzeit folgt den in Deutschland gültigen Regeln. Eine abweichende Locale-Einstellung über das WiFiManager-Setup ist in einer zukünftigen Version denkbar.

Die weitere Konfiguration geschieht über MQTT. Die dazu verwendeten Topics werden im Folgenden beschrieben. Im nicht-konfigurierten Zustand wird zunächst ausschließlich das Topic zur Konfiguration der Pumpenhardware empfangen, erst danach folgen die anderen Konfigurations- und Befehl-Topics.

Die Syntax lautet:

**Topic** `DosingFeeder/DosingFeeder-`_aabbcc_`/config/`_paramname_ **→ Message** _paramvalue_

### `pump_pins` → _configuration_

Diese Nachricht konfiguriert die Hardware der Pumpenansteuerung. Die Message ist eine mit Leerzeichen separierte Liste der GPIO-Ports für die Pumpen.

Die GPIO-Ports sind die rohen Pin-Nummern, also nicht die D-Nummern des Wemos D1 mini (Achtung: D2 = GPIO4, D4 = GPIO2).

Beispiel: drei Pumpen an GPIO2, GPIO3 und GPIO4 -> "`2 3 4`" (Leerzeichen vor und hinter der `3`, nicht am Anfang oder Ende der Nachricht)

Es wird empfohlen, die `config/pump`-Message als retained abzulegen, damit das Gerät sie direkt nach der Verbindungsaufnahme zum Broker einlesen kann.

Nachdem diese Message empfangen wurde, befindet sich das Gerät im konfigurierten Zustand, in dem die weiteren Parameter gesetzt werden können. Diese Reihenfolge ist notwendig, weil die weiteren Messages sich auf die zuvor konfigurierten Pumpenkanäle beziehen.

### `starttime/`_pumpidx_ → _timeofday_

Diese Nachricht konfiguriert die erste Uhrzeit des Tages, an der die Pumpe _pumpidx_ (Nummer von 0 bis Pumpenanzahl-1) gestartet werden soll. Der Wert ist im Format hh:mm:ss angegeben. Wird kein Startzeitpunkt gegeben, so gilt 00:00:00 (Tagesbeginn). Alternativ kann anstelle des numerischen Formats auch der Text "`now`" angegeben werden. In diesem Fall wird die aktuelle Zeit als Startzeit verwendet und die Pumpe sofort aktiviert, falls zuvor eine Dauer (oder ein Volumen) gesetzt wurde.

**Anmerkungen:**

1. Wenn bei Erreichen des Startzeitpunkts noch keine Dauer bzw. kein Volumen (letzteres ist derzeit noch nicht implementiert) gesetzt ist, wird die Startzeit ignoriert. Aus diesem Grund ist es nicht möglich, Startzeit und Volumen als retained Messages vorab zu setzen, denn hier ist die Reihenfolge des Empfangs undefiniert. Stattdessen kann der Befehl `params` verwendet werden, um die Parameter gleichzeitig zu setzen.
2. In der aktuellen Implementierung **muss** zunächst der Startzeitpunkt erreicht sein, um die Pumpe für den Tag zu aktivieren. Wenn also ein Startzeitpunkt gesetzt wird, der für den laufenden Tag bereits verstrichen ist, wird die Pumpe an diesem Tag nicht mehr aktiviert.
3. Es gibt eine Race Condition beim Startzeitpunkt `now`. Wenn der Sekundenwert hoch ist (59), kann es passieren, dass die Startzeit „verpasst“ wird und die Pumpe am laufenden Tag nicht mehr aktiviert wird.
4. Bei der Umstellung zwischen Normal- und Sommerzeit kann es zu unbeabsichtigten Effekten kommen. Die Startzeit wird laufend feldweise mit der tatsächlichen Zeit verglichen, bei Übereinstimmung wird der Zyklus begonnen. Am Tag der Umstellung auf Sommerzeit (März) wird daher, wenn die Startzeit zwischen 02:00:00 und 02:59:59 liegt, die Startbedingung _nicht_ eintreten. Umgekehrt wird beim Übergang auf Normalzeit (Oktober) ein Startzeitpunkt zwischen 00:00:00 und 02:59:59 noch als Sommerzeit interpretiert (Beispiel: 00:00 Uhr Startzeit und 2 Stunden Intervall wird dann um 00 und 02 Uhr Sommerzeit, dann 2 h später um 03 Uhr Normalzeit und danach zu den ungeraden vollen Stunden schalten, an anderen Tagen dagegen zu den geraden Stunden.) 

Die in Punkt 2, 3 und 4 beschriebenen Besonderheiten können in einer zukünftigen Softwareversion obsolet sein.

### `interval/`_pumpidx_ → _interval_

Diese Nachricht konfiguriert das zyklische Intervall, in dem die Pumpe _pumpidx_ (Nummer von 0 bis Pumpenanzahl-1) gestartet werden soll. Der Wert ist als Sekundenbetrag (float) gegeben. Wird kein Intervall bzw. der Wert 0 gegeben, so wird die Pumpe nur einmalig pro Tag eingeschaltet.

Dieser Befehl kann auch im laufenden Betrieb verwendet werden. Er gilt dann für nachfolgende Aktivierungen der Pumpe, die noch nicht als Auftrag in der Warteschlange stehen. Dies setzt voraus, dass bereits ein Intervall gesetzt wurde, d.h. wenn bei Eintreten der Startzeit kein Intervall gesetzt war, wird die Pumpe an diesem Tag nur einmalig zu diesem Zeitpunkt gestartet und dann nicht mehr, auch wenn später ein Intervall gesetzt wird.

Mit dem beschriebenen Mechanismus ist kein Intervall > 24 h definierbar. Dies kann ggf. zu einem späteren Zeitpunkt noch geändert werden.

### `duration/`_pumpidx_ → _duration_

Diese Nachricht konfiguriert die Dauer, für die die Pumpe _pumpidx_ (Nummer von 0 bis Pumpenanzahl-1) jeweils laufen soll. Der Wert ist als Sekundenbetrag (float) gegeben. Wurde keine Dauer bzw. der Wert 0 gegeben, wenn die Startzeit eintritt, so wird die Pumpe an diesem Tag nicht aktiviert. Die erreichbare Auflösung liegt im Bereich um 100 ms.

Dieser Befehl kann auch im laufenden Betrieb verwendet werden. Er gilt dann für nachfolgende Aktivierungen der Pumpe, die noch nicht als Auftrag in der Warteschlange stehen. Dies setzt voraus, dass bereits ein Intervall gesetzt wurde, d.h. wenn bei Eintreten der Startzeit kein Intervall gesetzt war, wird die Pumpe nur einmalig zu diesem Zeitpunkt gestartet und dann nicht mehr, so dass eine spätere Änderung der Dauer an diesem Tag unwirksam bleibt.

### `size/`_pumpidx_ → _size_

**(nicht implementiert)**

Diese Nachricht konfiguriert das Volumen, das über die Pumpe _pumpidx_ (Nummer von 0 bis Pumpenanzahl-1) jeweils abgegeben werden soll. Der Wert ist als Betrag (float) in Milliliter gegeben.
Diese Angabe ist alternativ zur Dauer zu verstehen, sie erfordert eine (konfigurierbare) Umrechnung in die Einschaltdauer.

Wird weder Dauer noch Volumen gegeben, so wird die Pumpe nicht aktiviert.

### `params/`_pumpidx_ → _starttime_`;i`_interval_`;d`_duration_ oder _starttime_`;i`_interval_`;s`_size_

Diese Nachricht konfiguriert gleichzeitig Startzeit, Intervall sowie entweder Dauer oder Volumen (letzteres ist noch nicht implementiert). Sie kann auch retained abgelegt werden, um eine Pumpenkonfiguration automatisch zu setzen.

## Befehle

Über Befehle kann unmittelbar eine Aktion auf dem Gerät ausgelöst werden.

Die Syntax lautet:

**Topic** `DosingFeeder/DosingFeeder-`_aabbcc_`/`_command_ **→ Message** _paramvalue_

### `reset`

Diese Nachricht löscht die Konfiguration, behält aber den Inhalt des EEPROMS, also die WiFi- und MQTT-Einstellungen, bei. Retained Messages werden anschließend erneut eingelesen und können zur Konfiguration verwendet werden.

### `restart`

Diese Nachricht löst einen Neustart des µControllers aus, entsprechend einem Druck auf den Reset-Taster oder einem Power-on-Reset.

### `shot/`_pumpidx_ → _duration_

Diese Nachricht startet die Pumpe _pumpidx_ (Nummer von 0 bis Pumpenanzahl-1) zum nächstmöglichen Zeitpunkt für die angegebene Zeitdauer. Der Wert ist als Sekundenbetrag (float) gegeben. Dieser Befehl ändert nichts an einem laufenden Intervall, sondern wird unabhängig ausgeführt.

## Statusmeldungen

Analog zu den Befehlen werden Statusmeldungen über MQTT geschickt.

Die Syntax lautet:

**Topic** `DosingFeeder/DosingFeeder-`_aabbcc_`/`_status_ **→ Message** _paramvalue_

### `status` → _statustext_

Diese Nachricht zeigt den aktuellen Gerätestatus. Mögliche Werte sind

- `online, waiting for configuration` — wird angezeigt, wenn das Gerät online ist, aber noch keine Pumpenkonfiguration bekommen hat
- `online, active` — wird angezeigt, wenn die Pumpenkonfiguration eingelesen wurde
- `offline` — wird über den Last-Will-Mechanismus gesetzt, wenn die MQTT-Verbindung zum Gerät getrennt wurde

### `status/ip` → _statustext_

Diese Nachricht zeigt die zugeteilte IP-Adresse des Geräts.

### `status/pump_pins` → _statustext_

Diese Nachricht zeigt die Pumpenkonfiguration des Geräts. Der Text besteht aus der Anzahl der Pumpen, gefolgt von `: `, gefolgt von einer Leerzeichen-separierten Liste der Portnummern.

### `status/version` → _statustext_

Diese Nachricht zeigt die Firmwareversion des Geräts.

### `status/time` → _statustext_

Diese Nachricht zeigt die aktuelle Uhrzeit im Format _hh:mm:ss_; sie wird alle 10 Sekunden aktualisiert und kann als Lebttelegramm verwendet werden.

### `status/pump/`_pumpidx_`state` → _statustext_

Diese Nachricht zeigt den Schaltzustand der jeweiligen Pumpe an. 0 = aus, 1 = an.

### `status/pump/`_pumpidx_`params` → _paramstext_

Diese Nachricht zeigt die Parametrierung der jeweiligen Pumpe an. Sie wird nach jeder Änderung der Paramentrierung gesendet. Das Format entspricht dem des `params`-Befehls, wobei die Uhrzeit immer als konkrete Uhrzeit angezeigt wird, auch wenn sie mit `now` gesetzt wurde. Zusätzlich wird der Zustand `pending` oder `not pending` als weiteres Feld angezeigt. Dieses Flag gibt an, ob die Pumpe am laufenden Tag noch auf ihre erstmalige Aktivierung wartet (pending) oder ob die Startzeit bereits verstrichen ist.

### `status/queue` → _queuetext_

Diese Nachricht zeigt die aktuelle Warteschlange für Pumpenjobs an. Sie wird nach jeder Änderung der Warteschlange gesendet.
