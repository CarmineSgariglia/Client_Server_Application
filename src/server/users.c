#include "server/users.h"

#include <string.h>

void users_init(user_db_t *db) {
    memset(db, 0, sizeof(*db));
}

int users_exists(const user_db_t *db, const char *nickname) {
    int i;
    for (i = 0; i < MAX_USERS; ++i) {
        if (db->items[i].in_use && strcmp(db->items[i].nickname, nickname) == 0) {
            return 1;
        }
    }
    return 0;
}

int users_register(user_db_t *db, const char *nickname, const char *password) {
    int i;

    if (!proto_valid_name(nickname, NICK_MAX) || !proto_valid_name(password, PASS_MAX)) {
        return -2;
    }
    if (users_exists(db, nickname)) {
        return -1;
    }
    for (i = 0; i < MAX_USERS; ++i) {
        if (!db->items[i].in_use) {
            strncpy(db->items[i].nickname, nickname, NICK_MAX);
            strncpy(db->items[i].password, password, PASS_MAX);
            db->items[i].nickname[NICK_MAX] = '\0';
            db->items[i].password[PASS_MAX] = '\0';
            db->items[i].in_use = 1;
            return 0;
        }
    }
    return -3;
}

int users_authenticate(const user_db_t *db, const char *nickname, const char *password) {
    int i;
    for (i = 0; i < MAX_USERS; ++i) {
        if (db->items[i].in_use &&
            strcmp(db->items[i].nickname, nickname) == 0 &&
            strcmp(db->items[i].password, password) == 0) {
            return 0;
        }
    }
    return -1;
}
