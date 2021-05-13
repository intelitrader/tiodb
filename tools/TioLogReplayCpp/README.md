# TioLogReplay

Lê um arquivo de log linha a linha e envia os dados para o servidor do Tio. Ele pode ser usado para gerar o sinal de delay de Market Data de 15 minutos, ao ler o arquivo do tio enquanto está sendo gravado, adicionando o parâmetro `--delay 900`.

## Build

A montagem dos solutions para Visual Studio (Windows) ou Makefile para Linux devem seguir o README principal do repositório.

## Execução

Uma vez compilado o projeto este é o modo de executá-lo:

```
TioLogReplay.exe --file-path <log-de-transacao-do-tio.log> [--address <localhost> --port <2605> --speed <speed-of-messages-sent> --delay <delay-in-seconds> --follow]
```

## Parâmetros

| Parâmetro                     | Description                                                  | Default   |
| ----------------------------- | ------------------------------------------------------------ | --------- |
| --file-path                   | Specify the full path for the log file. Or you can set "stdin" as input |           |
| --address                     | Defines the address for Tio server. (default=localhost)      | localhost |
| --port                        | Defines the port for Tio server.                             | 2605      |
| --delay                       | Message delay relative to original message time. We will wait if necessary to make sure the message will be replicated X second after the time message was written to log. | 0         |
| --speed                       | Speed that messages will be send to tio. 0 = as fast as possible. | 0         |
| --follow                      | Follow the file. Will wait for the file to grow to continue. |           |
| --max-network-batch-messages  | Max consecutive messages sent before network batch reset. | 1000 |
