#ifndef LOBBY_H
#define LOBBY_H

#include "../common/protocol.h"

#define MAX_LOBBY_NAME 64

typedef struct
{
    int id;

    char name[MAX_LOBBY_NAME];

    int host_id;

    int player_count;

    int active;

} Lobby;

#endif