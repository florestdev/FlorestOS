# FlorestOS-1.0 (FlorestDevelopment)

Учебная ОС с e1000, DNS, честным TCP-сканом, mcstatus, HTTP PUSH.

## Скан портов

`scan` шлёт **SYN**, ждёт:
- **OPEN** — пришёл SYN-ACK (потом RST)
- **closed** — пришёл RST
- **filtered** — тишина / timeout

## mcstatus

1. TCP Server List Ping → MOTD, version, protocol, players online/max, favicon flag, sample
2. UDP Query (GameSpy) → hostname, map, plugins, numplayers, список ников и т.д.

Иконка в VGA не рисуется (это base64 PNG в JSON); пишется, есть ли favicon.

## push

Только **HTTP** (не HTTPS):

```text
push 10.0.2.2 8080 /ingest
```

POST body = лог `results`.

## Сборка

```bash
make && make run
```
