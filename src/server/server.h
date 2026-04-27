#ifndef SERVER_H
#define SERVER_H

#include "server/game.h"
#include "server/users.h"

#include <time.h>

#define MAX_CLIENTS 16

typedef struct {
    int fd;
    int authenticated;
    int player_id;
    char nickname[NICK_MAX + 1];
    char inbuf[PROTO_MAX_LINE];
    size_t inbuf_len;
} client_session_t;

typedef struct {
    int listen_fd;
    int duration_sec;
    int period_sec;
    int running;
    time_t start_time;
    time_t next_update;
    user_db_t users;
    game_t game;
    client_session_t clients[MAX_CLIENTS];
} server_t;

int server_run(const char *port, int duration_sec, int period_sec);

#endif
