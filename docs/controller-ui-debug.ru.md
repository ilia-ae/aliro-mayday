# Веб-панель контроллера: локальный запуск и отладка

[English](controller-ui-debug.md) · [Документация](README.ru.md) · [Фото стенда](demo-stand.ru.md)

Настоящая панель администрирования на Svelte из `aliro-esp32/data` запускается
на компьютере с демонстрационными ответами HTTP/WebSocket. Контроллер,
ESP-IDF, USB, пароль Wi-Fi и сохранённые ключи не нужны. Это просмотр интерфейса;
аутентификация NFC, Matter, кошелёк и физический замок не эмулируются.

## Быстрый запуск

Установите [Bun](https://bun.sh/docs/installation) **1.3.13** и
[Node.js](https://nodejs.org/en/download) **22.12+** (рекомендуется 24 LTS).
Из корня репозитория:

```sh
cd aliro-esp32/data
bun run debug
```

Откройте **http://127.0.0.1:5173/**. При первом запуске зависимости
устанавливаются по сохранённому `bun.lock`; если пакетов нет в кеше,
потребуется интернет. В панели видна пометка **«Demo preview — sample data»**.
Статус Online относится к локальному демосерверу. Устройство, считыватель,
эмитент, сеть, время работы и журнал содержат примеры.
Прежнее название **HomeKey** сохранено в текущем интерфейсе.

Оба сервера слушают только `127.0.0.1`: демо-API получает свободный порт,
Vite — порт 5173. Скрипт не ищет устройства и не пересылает запросы контроллеру.
Сохранение настроек, сертификатов, перезагрузка, сброс и OTA возвращают ошибку
режима чтения; изменения не сохраняются. Пример GPIO не заменяет
[схему подключения](wiring.ru.md).

## Отладка и остановка

- Правьте `aliro-esp32/data/src/`: Vite автоматически обновляет страницу.
- В инструментах разработчика браузера доступны точки останова, Console,
  запросы Network и сообщения `/ws`. Раздел Logs получает демонстрационные записи.
- Примеры ответов API и запреты записи находятся в
  [controller-ui-demo.mjs](../scripts/controller-ui-demo.mjs).
  После изменения этого серверного файла перезапустите команду.
- **Ctrl+C** останавливает Vite и демо-API. Состояние контроллера не меняется.
- Если порт 5173 занят: `bun run debug --port 5174`.
  При занятом порте скрипт выдаёт ошибку, а не подменяет адрес другим портом.
- Справка: `bun run debug --help`.
- Требуемые версии `node` и `bun` должны быть доступны через `PATH`.
  Vite работает через Node.js, скрипт запуска и демо-API — через Bun.

Тот же скрипт можно запустить из корня репозитория:

```sh
bun scripts/debug-controller-ui.mjs --port 5173
```

## Проверки и обычная сборка интерфейса

Из `aliro-esp32/data`:

```sh
bun run debug:test
bun run build
```

Тесты проверяют чтение API, запреты записи и команд устройства, чтение и ping
WebSocket, настоящий прокси Vite и остановку серверов. CI выполняет эти проверки
и обычную сборку панели. `bun run build` создаёт привычные сжатые ресурсы
в `aliro-esp32/data/dist/`; по умолчанию деморежим выключен, демосервер
в сборку не входит. `bun run dev` сохраняет прежний прокси на localhost:8000.
Эти команды не прошивают контроллер. Установка прошивки — отдельная операция
из [руководства по прошивке](../aliro-esp32/README.ru.md).

## Галерея снимков

Шесть настоящих снимков браузера от **16 октября 2026**, сделанных через
локальный демоскрипт. Каждый показывает начальный экран основного раздела меню.
[Переключать снимки на сайте](https://aliro.mayday.software/?lang=ru#controller-ui)
или раскрыть нужный раздел ниже. Нажмите на картинку, чтобы открыть полный размер.

<details open>
<summary>Обзор · Dashboard</summary>

[![Обзор — демонстрационные данные](../aliro-site/frontend/public/images/controller-dashboard-demo.jpg)](../aliro-site/frontend/public/images/controller-dashboard-demo.jpg)

</details>

<details>
<summary>MQTT · MQTT</summary>

[![MQTT — демонстрационные данные](../aliro-site/frontend/public/images/controller-mqtt-demo.jpg)](../aliro-site/frontend/public/images/controller-mqtt-demo.jpg)

</details>

<details>
<summary>Действия · Actions</summary>

[![Действия — демонстрационные данные](../aliro-site/frontend/public/images/controller-actions-demo.jpg)](../aliro-site/frontend/public/images/controller-actions-demo.jpg)

</details>

<details>
<summary>Система · System</summary>

[![Система — демонстрационные данные](../aliro-site/frontend/public/images/controller-system-demo.jpg)](../aliro-site/frontend/public/images/controller-system-demo.jpg)

</details>

<details>
<summary>Обновление · OTA Update</summary>

[![Обновление — демонстрационные данные](../aliro-site/frontend/public/images/controller-ota-demo.jpg)](../aliro-site/frontend/public/images/controller-ota-demo.jpg)

</details>

<details>
<summary>Журнал · Logs</summary>

[![Журнал — демонстрационные данные](../aliro-site/frontend/public/images/controller-logs-demo.jpg)](../aliro-site/frontend/public/images/controller-logs-demo.jpg)

</details>

На снимках нет реального IP контроллера, кода сопряжения, паролей или ключевого
материала. Баннер обозначает демоданные, а Online относится к локальному демо-API.
Это демонстрация веб-панели, а не проверка подключённого устройства.
Файлы прошивки не загружались, обращений к физическому контроллеру не было.

Исходная панель сохраняет [лицензию MIT](../aliro-esp32/data/LICENSE).
Наши скрипт запуска, демосервер и тесты подпадают под
[лицензионные условия репозитория](licensing.ru.md).
Прежние коммерческие условия не меняются.
