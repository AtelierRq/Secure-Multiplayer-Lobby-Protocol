#ifndef SERVER_H
#define SERVER_H

#include <winsock2.h>
#include <windows.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "../common/protocol.h"
#include "lobby.h"

typedef struct
{
    int id;

    SOCKET socket_fd;

    SSL *ssl;

    char nickname[MAX_NICK_LEN];

    char token[MAX_TOKEN_LEN];

    ClientState state;

    int lobby_id;

} Client;

extern Client clients[MAX_CLIENTS];

extern CRITICAL_SECTION clients_mutex;

extern Lobby lobbies[MAX_LOBBIES];

extern CRITICAL_SECTION lobbies_mutex;

DWORD WINAPI client_thread(LPVOID arg);

int nickname_exists(const char *nickname);

void remove_client(int id);

#endif