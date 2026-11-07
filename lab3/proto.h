#ifndef PROTO_H
#define PROTO_H
#include <stdint.h>

#define MAXLINE  4096     
#define MAXMSG   (MAXLINE + 64)   // 64 байта на "error: \"" и хвост сообщения

#define LEN_EOF  (-1)    

// значения поля len:
//   > 0  - столько байт значимых в data
//     0  - (в to_parent) строка хорошая, сообщения нет
//    -1  - конец работы (LEN_EOF)

typedef struct {
    int32_t len; // >0 — сколько байт в data
    char    data[MAXMSG];
} slot_t;

typedef struct {
    slot_t to_child;          // parent -> child: строки
    slot_t to_parent;         // child  -> parent: ошибки
} shm_t;

#endif
