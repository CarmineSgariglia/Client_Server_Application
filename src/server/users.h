#ifndef USERS_H
#define USERS_H

#include "common/protocol.h"

#define MAX_USERS 64

typedef struct {
    char nickname[NICK_MAX + 1];
    char password[PASS_MAX + 1];
    int in_use;
} user_t;

typedef struct {
    user_t items[MAX_USERS];
} user_db_t;

void users_init(user_db_t *db);
int users_register(user_db_t *db, const char *nickname, const char *password);
int users_authenticate(const user_db_t *db, const char *nickname, const char *password);
int users_exists(const user_db_t *db, const char *nickname);

#endif
