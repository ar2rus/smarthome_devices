# Совместимость сборки Arduino IDE / Atmel Studio

Исправления по двум ошибкам сборки от 07.09.2026:

- `PE undeclared`: проект Bridge.cproj выбирает ATmega8A, где заголовок определяет `UPE`. В приложении Bridge используется непосредственно `UPE`, без макросов совместимости. Цель сборки — ATmega8A, как в Atmel Studio; загрузчик и настройки fuse не меняются.
- `SPIFFSEditor.h: No such file or directory`: файловый редактор есть не во всех поставках ESPAsyncWebServer. OptionalFileEditor.h определяет наличие заголовка и включает обработчик `/edit` только при его наличии. LittleFS, `/flash.html`, API прошивки и журнал от этого редактора не зависят. Для сборки без редактора можно явно задать `SMARTHOME_HAS_FILE_EDITOR=0`; на компиляторах без `__has_include` редактор по умолчанию выключен.

Обновить исходники smarthome_devices, включая новый OptionalFileEditor.h. Выбор ATmega8A в Atmel Studio менять не нужно. В Arduino IDE использовать ESP8266 Core 3.1.2 и изменённую библиотеку ClunetMulticast. Версии зависимостей на компьютере пользователя по фотографии установить нельзя; локальная проверка выполнена с ESP Async WebServer 2.1.2 и ArduinoJson 7.4.2.

Сообщение Arduino IDE о нескольких ArduinoJson показывает выбранную копию библиотеки и само по себе не является причиной показанной ошибки. Ошибка скачивания package_index.json относится к обновлению индекса пакетов; она не добавит отсутствующий SPIFFSEditor.h в библиотеку.

Проверки:

```sh
python3 tests/build_firmware.py --without-editor --output /tmp/bridge-compat-no-editor
python3 tests/build_firmware.py --output /tmp/bridge-compat-with-editor
```

Дополнительно проверена автоматическая детекция OptionalFileEditor.h препроцессором с присутствующим и отсутствующим SPIFFSEditor.h. Проверки не запускают прошивку устройств.
