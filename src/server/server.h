#ifndef SERVER_H
#define SERVER_H

#include "server/game.h"
#include "server/users.h"

#include <time.h>

// Struttura per rappresentare una sessione client connessa al server e le informazioni ad essa associate, come il file descriptor, lo stato di autenticazione, l'ID del giocatore associato, il nickname e un buffer per i dati in arrivo
typedef struct {
    int fd; 
    int authenticated; 
    int player_id;
    char nickname[NICK_MAX + 1];
    char inbuf[PROTO_MAX_LINE]; // Buffer per i dati in arrivo dal client
    size_t inbuf_len; // Lunghezza dei dati attualmente presenti nel buffer inbuf
} client_session_t;

// Struttura per rappresentare lo stato del server, inclusi il file descriptor di ascolto, la durata della partita, il periodo di aggiornamento, lo stato di esecuzione, il tempo di inizio, il tempo del prossimo aggiornamento, il database degli utenti, lo stato del gioco e l'elenco delle sessioni client connesse
typedef struct {
    int listen_fd;
    int duration_sec;
    int period_sec;
    int running; 
    time_t start_time;
    time_t next_update;
    user_db_t users; // Database degli utenti registrati
    game_t game; // Stato del gioco
    client_session_t *clients;
    size_t client_count;
    size_t client_capacity;
} server_t;

int server_run(const char *port, int duration_sec, int period_sec);

#endif
