#include "common/utils.h"
#include "server/server.h"

#include <stdio.h>
#include <stdlib.h>

//Server prende in input la porta su cui ascoltare, la durata della partita in secondi e il periodo di invio aggiornamenti ai client in secondi (opzionale, default 5 secondi)

int main(int argc, char **argv) {
    int ok;                 //Flag per indicare se la conversione da stringa a long è avvenuta con successo
    long duration = 300;    //durata della partita in secondi, default 5 minuti, default 300 secondi (opzionale)
    long period = 5;        //periodo di invio aggiornamenti ai client in secondi, default 5 secondi (opzionale)


    //Controllo argomenti da linea di comando
    if (argc < 2 || argc > 4) {
        //Se il numero di argomenti è minore di 2 o maggiore di 4, stampo un messaggio di errore e termino con codice di uscita 1
        fprintf(stderr, "Per avviare il server usare: %s <porta> [durata_secondi (default 300)] [periodo_secondi (default 5)]\n", argv[0]);
        return EXIT_FAILURE;
    }
    //Controllo validità durata
    if (argc >= 3) {
        //Se è stato specificato un argomento per la durata, lo converto da stringa a long e controllo che sia compreso tra 10 secondi e 24 ore (86400 secondi)
        duration = utils_parse_long(argv[2], 10, 86400, &ok);
        if (!ok) {
            fprintf(stderr, "Durata non valida\n");
            return EXIT_FAILURE;
        }
    }
    //Controllo validità periodo   
    if (argc == 4) {
        //Se è stato specificato un periodo, lo converto da stringa a long e controllo che sia compreso tra 1 e 3600 secondi (1 ora)
        period = utils_parse_long(argv[3], 1, 3600, &ok);
        if (!ok) {
            fprintf(stderr, "Periodo non valido\n");
            return EXIT_FAILURE;
        }
    }
   
    //Avvio server
    if (server_run(argv[1], (int)duration, (int)period) != 0) {
        //Se server_run restituisce un valore diverso da 0, significa che si è verificato un errore durante l'esecuzione del server
        fprintf(stderr, "Terminazione server con errore\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
