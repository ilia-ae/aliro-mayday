# Временные подтверждения снимка — 15 октября 2026

[English](README.md) · [Авторство](../../../AUTHORS.ru.md) · [Результат проверки](verification.json)

Подпись: **Mayday / Ilia Arestov**, только для собственного материала проекта.
Сторонние компоненты сохраняют своих авторов и лицензии.

Пакет относится к снимку
[`24e218ef1ea83dafd47447e0393ddd5cd949ccaa`](https://github.com/ilia-ae/aliro-mayday/tree/24e218ef1ea83dafd47447e0393ddd5cd949ccaa)
с обновлённой подписью и текстами сайта о лицензии v1.1.
[Манифест](authorship-manifest.json) связывает коммит, его `SOURCE_MANIFEST.json`
(2 396 файлов), лицензию и сведения об авторстве. SHA-256 манифеста:

```text
9f51d5b701905d34c7a22910c80c2950451cfa3cf9402e8485947b90ff13af2c
```

Через [deposit.mayday.software](https://deposit.mayday.software/) переданы только
хэш и запросы RFC 3161 с этим хэшем и случайным nonce. Ключи контроллера, резервные
копии и приватная история разработки в пакет не входят.

## Проверенное состояние

| Подтверждение | Результат при публикации |
| --- | --- |
| DigiCert RFC 3161 | Проверены подпись, хэш манифеста и nonce запроса; 15.10.2026 12:14:06 UTC |
| FreeTSA RFC 3161 | Проверены подпись, хэш и nonce по опубликованным сертификатам; 15.10.2026 12:14:07 UTC |
| OpenTimestamps Alice, Bob и Finney | Три читаемые квитанции календарей для одного хэша; **подтверждение блока Bitcoin ожидается** |
| Подпись личности автора | Подпись GPG или иная проверка личности автора не заявляется |

Проверка также отвергла изменённые байты манифеста. Дополнительный ответ Sectigo
не прошёл полную проверку OpenSSL и исключён из опубликованных проверенных
доказательств; причина указана в [результате проверки](verification.json).

Метки подтверждают существование и целостность этого заявления и указанного
снимка. Сами по себе они не устанавливают авторство, не передают права и не
подтверждают лицензирование или сертификацию Aliro. См.
[OpenTimestamps](https://opentimestamps.org/) и
[пояснения WIPO об авторском праве](https://www.wipo.int/en/web/copyright/faq-copyright).
Более поздний коммит с этим пакетом доказательств не входит в отмеченный снимок:
доказательство не заявляет, что оно зафиксировало само себя.

## Независимая проверка

Выполняйте команды из этой папки. Хэш должен совпасть с указанным выше:

```sh
openssl dgst -sha256 authorship-manifest.json
```

Для DigiCert используйте доверенный вами набор корневых сертификатов, а не
произвольный сертификат из полученного ответа:

```sh
openssl ts -verify -data authorship-manifest.json \
  -in authorship-manifest.json.tsr.digicert -CAfile /path/to/trusted-ca-bundle.pem
```

Включены публичные сертификаты CA и TSA от FreeTSA. Их файловые хэши совпали с
опубликованными на [официальной странице](https://freetsa.org/index_en.php);
значения сохранены в `verification.json`.

```sh
openssl ts -verify -data authorship-manifest.json \
  -in authorship-manifest.json.tsr.freetsa \
  -CAfile freetsa-ca.pem -untrusted freetsa-tsa.pem
openssl ts -verify -queryfile authorship-manifest.tsq \
  -in authorship-manifest.json.tsr.freetsa \
  -CAfile freetsa-ca.pem -untrusted freetsa-tsa.pem
```

Для OpenTimestamps можно посмотреть квитанцию, а позже обновить рабочую копию:

```sh
ots --no-cache info authorship-manifest.json.ots.alice
cp authorship-manifest.json.ots.alice /tmp/aliro-authorship.ots
ots --no-cache upgrade /tmp/aliro-authorship.ots
ots --no-cache verify -f authorship-manifest.json /tmp/aliro-authorship.ots
```

Для окончательной проверки нужен настроенный источник проверенных Bitcoin-блоков.
Квитанция со статусом pending или успешная отправка запроса ещё не подтверждает
блок. При проверке исходников сравните SHA-256 `SOURCE_MANIFEST.json` указанного
коммита с манифестом депонирования, затем его файловые хэши — с файлами снимка.
