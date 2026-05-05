#include "server/server.h"

#include "common/net.h"
#include "common/protocol.h"
#include "common/utils.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define INITIAL_CLIENTS_CAPACITY 16

static void client_reset(client_session_t *c) {
    c->fd = -1;
    c->authenticated = 0;
    c->player_id = -1;
    c->nickname[0] = '\0';
    c->inbuf_len = 0;
}

static void server_init(server_t *s, int listen_fd, int duration_sec, int period_sec) {
    memset(s, 0, sizeof(*s));
    s->listen_fd = listen_fd;
    s->duration_sec = duration_sec;
    s->period_sec = period_sec;
    s->running = 1;
    s->start_time = utils_now();
    s->next_update = s->start_time + period_sec;
    users_init(&s->users);
    game_init(&s->game);
}

static void server_free(server_t *s) {
    free(s->clients);
    users_free(&s->users);
    game_free(&s->game);
    s->clients = NULL;
    s->client_count = 0;
    s->client_capacity = 0;
}

static int server_reserve_clients(server_t *s, size_t needed) {
    client_session_t *new_clients;
    size_t i;
    size_t old_capacity = s->client_capacity;
    size_t new_capacity = old_capacity == 0 ? INITIAL_CLIENTS_CAPACITY : old_capacity;

    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity == old_capacity) {
        return 0;
    }
    new_clients = realloc(s->clients, new_capacity * sizeof(*new_clients));
    if (new_clients == NULL) {
        return -1;
    }
    s->clients = new_clients;
    for (i = old_capacity; i < new_capacity; ++i) {
        client_reset(&s->clients[i]);
    }
    s->client_capacity = new_capacity;
    return 0;
}

static void disconnect_client(server_t *s, int index);

static int sendf(client_session_t *c, const char *fmt, ...) {
    char payload[PROTO_MAX_LINE];
    char line[PROTO_MAX_LINE];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(payload, sizeof(payload), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }
    if (proto_make_line(line, sizeof(line), "%s", payload) < 0) {
        return -1;
    }
    return net_send_line(c->fd, line);
}

static int send_local(server_t *s, client_session_t *c) {
    char map[512];
    if (c->authenticated && c->player_id >= 0) {
        game_build_local_map(&s->game, c->player_id, map, sizeof(map));
        return sendf(c, "S2C_LOCAL_MAP %d %d %s", LOCAL_VIEW_W, LOCAL_VIEW_H, map);
    }
    return 0;
}

static int send_global_to_client(server_t *s, client_session_t *c) {
    char map[512];
    char pos[512];
    if (c->fd >= 0 && c->authenticated) {
        game_build_global_map(&s->game, map, sizeof(map));
        game_build_positions(&s->game, pos, sizeof(pos));
        return sendf(c, "S2C_GLOBAL_UPDATE %d %d %s %s", MAP_W, MAP_H, map, pos);
    }
    return 0;
}

static void broadcast_global(server_t *s) {
    size_t i;
    for (i = 0; i < s->client_count; ++i) {
        if (send_global_to_client(s, &s->clients[i]) != 0) {
            disconnect_client(s, i);
        }
    }
}

static void disconnect_client(server_t *s, int index) {
    client_session_t *c = &s->clients[index];
    if (c->fd >= 0) {
        close(c->fd);
    }
    if (c->authenticated && c->player_id >= 0) {
        game_remove_player(&s->game, c->player_id);
    }
    client_reset(c);
}

static void accept_client(server_t *s) {
    int fd;
    size_t i;
    size_t slot;

    fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) {
        return;
    }
    for (i = 0; i < s->client_count; ++i) {
        if (s->clients[i].fd < 0) {
            client_reset(&s->clients[i]);
            s->clients[i].fd = fd;
            sendf(&s->clients[i], "S2C_OK CONNECTED");
            return;
        }
    }
    slot = s->client_count;
    if (server_reserve_clients(s, s->client_count + 1) != 0) {
        net_send_line(fd, "S2C_ERR SERVER_FULL\n");
        close(fd);
        return;
    }
    s->client_count++;
    client_reset(&s->clients[slot]);
    s->clients[slot].fd = fd;
    sendf(&s->clients[slot], "S2C_OK CONNECTED");
}

static int require_auth(client_session_t *c) {
    if (!c->authenticated) {
        sendf(c, "S2C_ERR NOT_AUTHENTICATED");
        return 0;
    }
    return 1;
}

static void handle_register(server_t *s, client_session_t *c, char **tok, int ntok) {
    int rc;
    if (ntok != 3) {
        sendf(c, "S2C_ERR BAD_SYNTAX");
        return;
    }
    rc = users_register(&s->users, tok[1], tok[2]);
    if (rc == 0) {
        sendf(c, "S2C_OK REGISTERED");
    } else if (rc == -1) {
        sendf(c, "S2C_ERR USER_EXISTS");
    } else if (rc == -2) {
        sendf(c, "S2C_ERR INVALID_CREDENTIALS");
    } else {
        sendf(c, "S2C_ERR USER_DB_FULL");
    }
}

static void handle_login(server_t *s, client_session_t *c, char **tok, int ntok) {
    int player_id;
    if (ntok != 3) {
        sendf(c, "S2C_ERR BAD_SYNTAX");
        return;
    }
    if (c->authenticated) {
        sendf(c, "S2C_ERR ALREADY_AUTHENTICATED");
        return;
    }
    if (users_authenticate(&s->users, tok[1], tok[2]) != 0) {
        sendf(c, "S2C_ERR AUTH_FAILED");
        return;
    }
    if (game_find_player(&s->game, tok[1]) >= 0) {
        sendf(c, "S2C_ERR USER_ALREADY_ONLINE");
        return;
    }
    player_id = game_add_player(&s->game, tok[1]);
    if (player_id < 0) {
        sendf(c, "S2C_ERR GAME_FULL");
        return;
    }
    c->authenticated = 1;
    c->player_id = player_id;
    strncpy(c->nickname, tok[1], NICK_MAX);
    c->nickname[NICK_MAX] = '\0';
    sendf(c, "S2C_OK LOGGED_IN %d %d", s->game.players[player_id].x, s->game.players[player_id].y);
    send_local(s, c);
    broadcast_global(s);
}

static void handle_move(server_t *s, client_session_t *c, char **tok, int ntok) {
    direction_t dir;
    int rc;
    if (!require_auth(c)) {
        return;
    }
    if (ntok != 2 || proto_parse_direction(tok[1], &dir) != 0) {
        sendf(c, "S2C_ERR BAD_DIRECTION");
        return;
    }
    rc = game_move(&s->game, c->player_id, dir);
    if (rc == 0) {
        sendf(c, "S2C_OK MOVED %d %d", s->game.players[c->player_id].x, s->game.players[c->player_id].y);
    } else if (rc == -2) {
        sendf(c, "S2C_ERR OUT_OF_BOUNDS");
    } else if (rc == -3) {
        sendf(c, "S2C_ERR WALL");
    } else if (rc == -4) {
        sendf(c, "S2C_ERR OCCUPIED");
    } else {
        sendf(c, "S2C_ERR MOVE_FAILED");
    }
    send_local(s, c);
}

static void handle_users(server_t *s, client_session_t *c) {
    char pos[512];
    if (!require_auth(c)) {
        return;
    }
    game_build_positions(&s->game, pos, sizeof(pos));
    sendf(c, "S2C_USERS %s", pos);
}

static void handle_line(server_t *s, int index, char *line) {
    client_session_t *c = &s->clients[index];
    char *tok[PROTO_MAX_TOKENS];
    int ntok = proto_split(line, tok, PROTO_MAX_TOKENS);

    if (ntok == 0) {
        return;
    }
    if (strcmp(tok[0], "C2S_REGISTER") == 0) {
        handle_register(s, c, tok, ntok);
    } else if (strcmp(tok[0], "C2S_LOGIN") == 0) {
        handle_login(s, c, tok, ntok);
    } else if (strcmp(tok[0], "C2S_MOVE") == 0) {
        handle_move(s, c, tok, ntok);
    } else if (strcmp(tok[0], "C2S_LIST_USERS") == 0 && ntok == 1) {
        handle_users(s, c);
    } else if (strcmp(tok[0], "C2S_LOCAL_MAP") == 0 && ntok == 1) {
        if (require_auth(c)) {
            send_local(s, c);
        }
    } else if (strcmp(tok[0], "C2S_GLOBAL_MAP") == 0 && ntok == 1) {
        if (require_auth(c)) {
            if (send_global_to_client(s, c) != 0) {
                disconnect_client(s, index);
            }
        }
    } else if (strcmp(tok[0], "C2S_QUIT") == 0 && ntok == 1) {
        sendf(c, "S2C_OK BYE");
        disconnect_client(s, index);
    } else if (strncmp(tok[0], "C2S_", 4) == 0) {
        sendf(c, "S2C_ERR BAD_SYNTAX");
    } else {
        sendf(c, "S2C_ERR UNKNOWN_COMMAND");
    }
}

static int read_client(server_t *s, int index) {
    client_session_t *c = &s->clients[index];
    char tmp[512];
    ssize_t n;

    n = recv(c->fd, tmp, sizeof(tmp), 0);
    if (n <= 0) {
        disconnect_client(s, index);
        return -1;
    }
    if (c->inbuf_len + (size_t)n >= sizeof(c->inbuf)) {
        sendf(c, "S2C_ERR LINE_TOO_LONG");
        disconnect_client(s, index);
        return -1;
    }
    memcpy(c->inbuf + c->inbuf_len, tmp, (size_t)n);
    c->inbuf_len += (size_t)n;
    c->inbuf[c->inbuf_len] = '\0';

    while (1) {
        char *nl = memchr(c->inbuf, '\n', c->inbuf_len);
        size_t line_len;
        char line[PROTO_MAX_LINE];
        if (nl == NULL) {
            break;
        }
        line_len = (size_t)(nl - c->inbuf);
        if (line_len >= sizeof(line)) {
            disconnect_client(s, index);
            return -1;
        }
        memcpy(line, c->inbuf, line_len);
        line[line_len] = '\0';
        memmove(c->inbuf, nl + 1, c->inbuf_len - line_len - 1);
        c->inbuf_len -= line_len + 1;
        c->inbuf[c->inbuf_len] = '\0';
        handle_line(s, index, line);
        if (c->fd < 0) {
            return -1;
        }
    }
    return 0;
}

static void broadcast_game_over(server_t *s) {
    size_t i;
    char winner[NICK_MAX + 1];
    char scores[512];
    int score;

    game_winner(&s->game, winner, sizeof(winner), &score);
    game_build_scores(&s->game, scores, sizeof(scores));
    for (i = 0; i < s->client_count; ++i) {
        if (s->clients[i].fd >= 0) {
            sendf(&s->clients[i], "S2C_GAME_OVER %s %d %s", winner, score, scores);
        }
    }
}

static long seconds_until_next_event(server_t *s) {
    time_t now = utils_now();
    time_t end = s->start_time + s->duration_sec;
    time_t next = s->next_update < end ? s->next_update : end;
    if (next <= now) {
        return 0;
    }
    return (long)(next - now);
}

// Funzione principale per eseguire il server, che prende in input la porta su cui ascoltare, la durata della partita in secondi (opzionale default 5 min) e il periodo di invio aggiornamenti ai client in secondi (opzionale, default 5 secondi)
int server_run(const char *port, int duration_sec, int period_sec) {
    server_t s;
    int listen_fd = net_create_server_socket(port); // Creo un socket server TCP che ascolta sulla porta specificata. Se si verifica un errore durante la creazione del socket, restituisco -1

    if (listen_fd < 0) {
        return -1;
    }
    signal(SIGPIPE, SIG_IGN); // Ignoro il segnale SIGPIPE che potrebbe essere generato quando si tenta di scrivere su un socket chiuso da parte del client
    server_init(&s, listen_fd, duration_sec, period_sec); 

    while (s.running) {
        fd_set rfds;
        struct timeval tv;
        int maxfd = s.listen_fd;
        int rc;
        size_t i;
        time_t now;

        FD_ZERO(&rfds);
        FD_SET(s.listen_fd, &rfds);
        for (i = 0; i < s.client_count; ++i) {
            if (s.clients[i].fd >= 0) {
                FD_SET(s.clients[i].fd, &rfds);
                if (s.clients[i].fd > maxfd) {
                    maxfd = s.clients[i].fd;
                }
            }
        }

        tv.tv_sec = seconds_until_next_event(&s);
        tv.tv_usec = 0;
        rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc > 0 && FD_ISSET(s.listen_fd, &rfds)) {
            accept_client(&s);
        }
        for (i = 0; i < s.client_count; ++i) {
            if (s.clients[i].fd >= 0 && FD_ISSET(s.clients[i].fd, &rfds)) {
                read_client(&s, i);
            }
        }

        now = utils_now();
        if (now >= s.next_update) {
            broadcast_global(&s);
            s.next_update = now + s.period_sec;
        }
        if (now >= s.start_time + s.duration_sec) {
            broadcast_game_over(&s);
            s.running = 0;
        }
    }

    for (size_t i = 0; i < s.client_count; ++i) {
        disconnect_client(&s, i);
    }
    close(s.listen_fd);
    server_free(&s);
    return 0;
}
