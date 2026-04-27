#include "server/game.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WALL_PERCENT 18
#define MIN_FREE_CELLS (MAX_PLAYERS * 3)
#define MAP_CELLS (MAP_W * MAP_H)

static int in_bounds(int x, int y) {
    return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H;
}

static void append_text(char *out, size_t out_size, const char *text) {
    size_t used = strlen(out);
    if (used < out_size) {
        snprintf(out + used, out_size - used, "%s", text);
    }
}

static char symbol_for_slot(int slot) {
    static const char symbols[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    if (slot >= 0 && slot < (int)(sizeof(symbols) - 1)) {
        return symbols[slot];
    }
    return '?';
}

static unsigned int random_seed(void) {
    unsigned int seed;
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd >= 0) {
        ssize_t n = read(fd, &seed, sizeof(seed));
        close(fd);
        if (n == (ssize_t)sizeof(seed)) {
            return seed;
        }
    }

    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        seed = (unsigned int)ts.tv_nsec;
        seed ^= (unsigned int)ts.tv_sec;
        seed ^= (unsigned int)getpid() << 16;
        return seed;
    }
}

static int count_free_cells(const game_t *game) {
    int y;
    int x;
    int count = 0;

    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            if (!game->wall[y][x]) {
                count++;
            }
        }
    }
    return count;
}

static int connected_free_cells(const game_t *game) {
    int visited[MAP_H][MAP_W];
    int qx[MAP_CELLS];
    int qy[MAP_CELLS];
    int head = 0;
    int tail = 0;
    int start_found = 0;
    int count = 0;
    int y;
    int x;

    memset(visited, 0, sizeof(visited));
    for (y = 0; y < MAP_H && !start_found; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            if (!game->wall[y][x]) {
                qx[tail] = x;
                qy[tail] = y;
                tail++;
                visited[y][x] = 1;
                start_found = 1;
                break;
            }
        }
    }

    while (head < tail) {
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        int i;
        int cx = qx[head];
        int cy = qy[head];
        head++;
        count++;

        for (i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (in_bounds(nx, ny) && !visited[ny][nx] && !game->wall[ny][nx]) {
                visited[ny][nx] = 1;
                qx[tail] = nx;
                qy[tail] = ny;
                tail++;
            }
        }
    }

    return count;
}

static int walls_are_playable(const game_t *game) {
    int free_cells = count_free_cells(game);
    if (free_cells < MIN_FREE_CELLS) {
        return 0;
    }
    return connected_free_cells(game) == free_cells;
}

static void generate_random_walls(game_t *game) {
    int attempt;
    int y;
    int x;

    for (attempt = 0; attempt < 100; ++attempt) {
        for (y = 0; y < MAP_H; ++y) {
            for (x = 0; x < MAP_W; ++x) {
                game->wall[y][x] = (rand() % 100) < WALL_PERCENT;
            }
        }
        if (walls_are_playable(game)) {
            return;
        }
    }

    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            game->wall[y][x] = 0;
        }
    }
    for (y = 1; y < MAP_H - 1; y += 3) {
        for (x = 2; x < MAP_W - 2; x += 5) {
            game->wall[y][x] = 1;
        }
    }
}

static int find_free_spawn(const game_t *game, int *x, int *y) {
    int yy;
    int xx;
    int p;
    int occupied;
    int free_count = 0;
    int chosen;

    for (yy = 0; yy < MAP_H; ++yy) {
        for (xx = 0; xx < MAP_W; ++xx) {
            if (game->wall[yy][xx]) {
                continue;
            }
            occupied = 0;
            for (p = 0; p < MAX_PLAYERS; ++p) {
                if (game->players[p].active && game->players[p].x == xx && game->players[p].y == yy) {
                    occupied = 1;
                    break;
                }
            }
            if (!occupied) {
                free_count++;
            }
        }
    }

    if (free_count == 0) {
        return -1;
    }

    chosen = rand() % free_count;
    for (yy = 0; yy < MAP_H; ++yy) {
        for (xx = 0; xx < MAP_W; ++xx) {
            if (game->wall[yy][xx]) {
                continue;
            }
            occupied = 0;
            for (p = 0; p < MAX_PLAYERS; ++p) {
                if (game->players[p].active && game->players[p].x == xx && game->players[p].y == yy) {
                    occupied = 1;
                    break;
                }
            }
            if (!occupied) {
                if (chosen == 0) {
                    *x = xx;
                    *y = yy;
                    return 0;
                }
                chosen--;
            }
        }
    }

    return -1;
}

void game_init(game_t *game) {
    int y;
    int x;

    memset(game, 0, sizeof(*game));
    srand(random_seed());
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            game->owner[y][x] = -1;
        }
    }

    generate_random_walls(game);
}

int game_add_player(game_t *game, const char *nickname) {
    int i;
    int slot = -1;
    int x = 0;
    int y = 0;

    if (game_find_player(game, nickname) >= 0 || find_free_spawn(game, &x, &y) != 0) {
        return -1;
    }
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (game->players[i].used && strcmp(game->players[i].nickname, nickname) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < MAX_PLAYERS; ++i) {
            if (!game->players[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return -1;
    }

    if (!game->players[slot].used) {
        memset(&game->players[slot], 0, sizeof(game->players[slot]));
        strncpy(game->players[slot].nickname, nickname, NICK_MAX);
        game->players[slot].nickname[NICK_MAX] = '\0';
        game->players[slot].symbol = symbol_for_slot(slot);
        game->players[slot].used = 1;
    }
    game->players[slot].active = 1;
    game->players[slot].x = x;
    game->players[slot].y = y;
    game->owner[y][x] = slot;
    game_reveal_around(game, slot);
    return slot;
}

void game_remove_player(game_t *game, int player_id) {
    if (player_id >= 0 && player_id < MAX_PLAYERS) {
        game->players[player_id].active = 0;
    }
}

int game_find_player(const game_t *game, const char *nickname) {
    int i;
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (game->players[i].active && strcmp(game->players[i].nickname, nickname) == 0) {
            return i;
        }
    }
    return -1;
}

void game_reveal_around(game_t *game, int player_id) {
    int dx;
    int dy;
    int x;
    int y;
    player_t *p;

    if (player_id < 0 || player_id >= MAX_PLAYERS || !game->players[player_id].active) {
        return;
    }
    p = &game->players[player_id];
    for (dy = -1; dy <= 1; ++dy) {
        for (dx = -1; dx <= 1; ++dx) {
            x = p->x + dx;
            y = p->y + dy;
            if (in_bounds(x, y) && game->wall[y][x]) {
                p->discovered_walls[y][x] = 1;
            }
        }
    }
}

int game_move(game_t *game, int player_id, direction_t dir) {
    int nx;
    int ny;
    int i;
    player_t *p;

    if (player_id < 0 || player_id >= MAX_PLAYERS || !game->players[player_id].active) {
        return -1;
    }
    p = &game->players[player_id];
    nx = p->x;
    ny = p->y;
    if (dir == DIR_UP) {
        --ny;
    } else if (dir == DIR_DOWN) {
        ++ny;
    } else if (dir == DIR_LEFT) {
        --nx;
    } else if (dir == DIR_RIGHT) {
        ++nx;
    }

    if (!in_bounds(nx, ny)) {
        return -2;
    }
    if (game->wall[ny][nx]) {
        p->discovered_walls[ny][nx] = 1;
        game_reveal_around(game, player_id);
        return -3;
    }
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (i != player_id && game->players[i].active &&
            game->players[i].x == nx && game->players[i].y == ny) {
            return -4;
        }
    }
    p->x = nx;
    p->y = ny;
    game->owner[ny][nx] = player_id;
    game_reveal_around(game, player_id);
    return 0;
}

void game_build_local_map(const game_t *game, int player_id, char *out, size_t out_size) {
    int y;
    int x;
    int owner;
    char row[MAP_W + 2];

    out[0] = '\0';
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            if (game->players[player_id].x == x && game->players[player_id].y == y) {
                row[x] = '@';
            } else if (game->players[player_id].discovered_walls[y][x]) {
                row[x] = '#';
            } else {
                owner = game->owner[y][x];
                row[x] = owner >= 0 && game->players[owner].used
                    ? game->players[owner].symbol
                    : '.';
            }
        }
        row[MAP_W] = (y == MAP_H - 1) ? '\0' : '/';
        row[MAP_W + 1] = '\0';
        append_text(out, out_size, row);
    }
}

void game_build_global_map(const game_t *game, char *out, size_t out_size) {
    int y;
    int x;
    int owner;
    char row[MAP_W + 2];

    out[0] = '\0';
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            owner = game->owner[y][x];
            row[x] = owner >= 0 && game->players[owner].used
                ? game->players[owner].symbol
                : '.';
        }
        row[MAP_W] = (y == MAP_H - 1) ? '\0' : '/';
        row[MAP_W + 1] = '\0';
        append_text(out, out_size, row);
    }
}

void game_build_positions(const game_t *game, char *out, size_t out_size) {
    int i;
    char tmp[96];

    out[0] = '\0';
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (game->players[i].active) {
            if (out[0] != '\0') {
                append_text(out, out_size, ",");
            }
            snprintf(tmp, sizeof(tmp), "%s:%c:%d:%d",
                     game->players[i].nickname,
                     game->players[i].symbol,
                     game->players[i].x,
                     game->players[i].y);
            append_text(out, out_size, tmp);
        }
    }
    if (out[0] == '\0') {
        append_text(out, out_size, "-");
    }
}

void game_build_scores(const game_t *game, char *out, size_t out_size) {
    int scores[MAX_PLAYERS];
    int y;
    int x;
    int i;
    char tmp[96];

    memset(scores, 0, sizeof(scores));
    out[0] = '\0';
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            if (game->owner[y][x] >= 0 && game->players[game->owner[y][x]].used) {
                scores[game->owner[y][x]]++;
            }
        }
    }
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (game->players[i].used) {
            if (out[0] != '\0') {
                append_text(out, out_size, ",");
            }
            snprintf(tmp, sizeof(tmp), "%s:%d", game->players[i].nickname, scores[i]);
            append_text(out, out_size, tmp);
        }
    }
    if (out[0] == '\0') {
        append_text(out, out_size, "-");
    }
}

int game_winner(const game_t *game, char *nickname, size_t nickname_size, int *score) {
    int scores[MAX_PLAYERS];
    int best = -1;
    int best_score = -1;
    int y;
    int x;
    int i;

    memset(scores, 0, sizeof(scores));
    for (y = 0; y < MAP_H; ++y) {
        for (x = 0; x < MAP_W; ++x) {
            if (game->owner[y][x] >= 0 && game->players[game->owner[y][x]].used) {
                scores[game->owner[y][x]]++;
            }
        }
    }
    for (i = 0; i < MAX_PLAYERS; ++i) {
        if (game->players[i].used && scores[i] > best_score) {
            best = i;
            best_score = scores[i];
        }
    }
    if (best < 0) {
        snprintf(nickname, nickname_size, "-");
        *score = 0;
        return -1;
    }
    snprintf(nickname, nickname_size, "%s", game->players[best].nickname);
    *score = best_score;
    return 0;
}
