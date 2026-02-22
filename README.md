# Projeto-RCI

join test:

    terminal A:
        ncat -u -l 59000 --exec "/bin/echo OKREG DONE"

    terminal B:
        make clean
        make
        ./OWR 127.0.0.1 58000 127.0.0.1 59000
        j 001 10

    terminal A termina processo

    output terminal B:
        Li o comando: j 001 10
        Sucesso: Nó registado no servidor.

leave test:
(first join)
    terminal A:
        ncat -u -l 59000 --exec "/bin/echo OKUNREG DONE"

    terminal B:
        leave (l)

    terminal A termina processo

    output terminal B:
        Li o comando: leave (l)
        Saida da rede 001 concluida

exit test:
(first join)
    terminal A:
        ncat -u -l 59000 --exec "/bin/echo OKUNREG DONE"

    terminal B:
        x

    terminal A termina processo

    output terminal B:
        Li comando: x
        A terminar aplicação

    terminal B termina processo
