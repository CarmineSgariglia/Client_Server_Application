# Piano di test manuale

## 1. Build

```sh
make clean
make
make test
```

Atteso: vengono generati `bin/server` e `bin/client` senza errori; lo smoke test registra due utenti, esegue login, chiede utenti/mappa globale e verifica che il server non scriva su stdout.

## 2. Avvio server

```sh
./bin/server 4242 60 5
```

Atteso: il server non stampa su stdout e resta in ascolto.

## 3. Connessione client

```sh
./bin/client 127.0.0.1 4242
```

Atteso: il client mostra help e messaggio di connessione.

## 4. Registrazione e login

Client 1:

```txt
register alice pass1
login alice pass1
```

Client 2:

```txt
register bob pass2
login bob pass2
```

Atteso: entrambi ricevono `S2C_OK`, mappa locale e aggiornamenti globali.

## 5. Movimento e conquista

Nel client 1:

```txt
d
d
s
local
global
```

Atteso: la posizione cambia, la mappa locale marca `@`, la mappa globale mostra celle conquistate con il simbolo del giocatore.

## 6. Movimento contro muro

La mappa server contiene muri fissi, ma lo spawn e casuale. Dopo il login leggere le coordinate iniziali da `OK: LOGGED_IN x y`, poi usare la mappa locale e muoversi verso un `#` quando viene scoperto. In alternativa, ripetere movimenti esplorativi finche un ostacolo adiacente appare nella mappa locale.

Atteso: il server risponde `S2C_ERR WALL`, il client resta fermo e nella mappa locale appare `#` per il muro scoperto.

## 7. Lista utenti

```txt
users
```

Atteso: elenco `nickname:simbolo:x:y` degli utenti collegati. Se due nickname iniziano con la stessa lettera, i simboli devono comunque essere diversi.

## 8. Aggiornamento periodico

Attendere il periodo configurato.

Atteso: ogni client riceve `S2C_GLOBAL_UPDATE` senza inviare comandi.

## 9. Disconnessione

```txt
quit
```

Atteso: il client termina pulitamente e gli altri client vedono la lista posizioni aggiornata al successivo update.

## 10. Timeout partita

Avviare il server con durata breve:

```sh
./bin/server 4242 20 5
```

Atteso: dopo circa 20 secondi i client ricevono `S2C_GAME_OVER` con vincitore e punteggi.

## 11. Docker

```sh
docker compose up --build server
docker compose run --rm client
```

Atteso: il client si connette al servizio `server` sulla porta `4242`.
