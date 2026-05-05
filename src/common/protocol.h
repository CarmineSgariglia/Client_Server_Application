#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define PROTO_MAX_LINE 4096 // Lunghezza massima di una linea del protocollo, inclusa la terminazione con '\n' e il carattere di fine stringa '\0'
#define PROTO_MAX_TOKENS 32 // Numero massimo di token che una linea del protocollo può contenere, incluso il comando e i suoi argomenti. Se una linea contiene più di questo numero di token, i token in eccesso verranno ignorati.
#define NICK_MAX 31 // Lunghezza massima di nickname e password, escluso il carattere di fine stringa '\0'
#define PASS_MAX 31 // Lunghezza massima di nickname e password, escluso il carattere di fine stringa '\0'

typedef enum {
    DIR_UP = 0,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} direction_t;

int proto_valid_name(const char *s, size_t max_len);
int proto_parse_direction(const char *s, direction_t *dir);
const char *proto_direction_name(direction_t dir);
int proto_split(char *line, char **tokens, int max_tokens);
int proto_make_line(char *dst, size_t dst_size, const char *fmt, ...);

#endif
