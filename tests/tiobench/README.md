# Testes unitários

ATENÇÃO: como o tioclient.py ainda não foi convertido, para rodar os testes em python da pasta test é necessário Python versão 2.7.

Os binários tio e tiobench devem estar disponíveis nas variáveis de ambiente TIO_PATH e TIO_BENCH_PATH, assim como o tioclient.py deve estar disponível para o Python. Se foi feito o build a partir da pasta tiodb/build, por exemplo, você pode definir essas variáveis da seguinte forma:

Windows:

    set PYTHONPATH=C:\intelitrader\src\tiodb\client\python
    set TIO_PATH=C:\intelitrader\src\tiodb\bin\x64\Release\tio.exe
    set TIO_BENCH_PATH=C:\intelitrader\src\tiodb\build\tests\tiobench\RelWithDebInfo\tiobench.exe

Linux:

    export PYTHONPATH=/mnt/c/intelitrader/src/tiodb/client/python
    export TIO_PATH=/mnt/c/intelitrader/src/tiodb/buildx/server/tio/tio
    export TIO_BENCH_PATH=/mnt/c/intelitrader/src/tiodb/buildx/tests/tiobench/tiobench

Em um terminal no diretório tiobench rodar o seguinte comando:

Windows:

    python -m test

Linux:

    python2 -m test

# Explicação dos testes (WIP)


| Teste | Descrição |
| ------ | ------ |
| umdf-feeder-test-data-stress| Teste de estresse, ele busca medir a performance do tio em situações extremas (8 clientes e 16 subscribers, por exemplo). No começo é feito um primeiro teste mais simples para gerar uma baseline, o resultado que aparece no output é a comparação de outros testes que são feitos aumentando de maneira exponencial os clientes/subscribers. Nos testes de single volatile list oq define se o teste deu errado ou não é uma comparação entre quantos eventos foram disparados e quantos eventos deveriam ser disparados. |

