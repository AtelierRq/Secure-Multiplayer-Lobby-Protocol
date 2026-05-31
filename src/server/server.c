#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "server.h"

// #pragma comment(lib, "ws2_32.lib")

Client clients[MAX_CLIENTS];
Lobby lobbies[MAX_LOBBIES];

CRITICAL_SECTION clients_mutex;
CRITICAL_SECTION lobbies_mutex;

static int next_client_id = 1;
static int next_lobby_id = 1;

/* -------------------------------------------------- */
/* TLS                                                */
/* -------------------------------------------------- */

SSL_CTX *create_server_context(void)
{
    SSL_CTX *ctx;

    ctx = SSL_CTX_new(TLS_server_method());

    if (!ctx)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_server_context(SSL_CTX *ctx)
{
    if (SSL_CTX_use_certificate_file(
            ctx,
            "certs/server_cert.pem",
            SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(
            ctx,
            "certs/server_key.pem",
            SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (!SSL_CTX_check_private_key(ctx))
    {
        fprintf(stderr,
                "Private key does not match certificate\n");
        exit(EXIT_FAILURE);
    }
}

/* -------------------------------------------------- */
/* CLIENT MANAGEMENT                                  */
/* -------------------------------------------------- */

int nickname_exists(const char *nickname)
{
    int i;

    EnterCriticalSection(&clients_mutex);

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].id == 0)
            continue;

        if (strcmp(clients[i].nickname, nickname) == 0)
        {
            LeaveCriticalSection(&clients_mutex);
            return 1;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    return 0;
}

void remove_client(int id)
{
    int i;

    EnterCriticalSection(&clients_mutex);

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].id == id)
        {
            clients[i].id = 0;

            memset(clients[i].nickname, 0,
                   sizeof(clients[i].nickname));

            memset(clients[i].token, 0,
                   sizeof(clients[i].token));

            clients[i].state = STATE_CONNECTED;

            clients[i].lobby_id = -1;

            break;
        }
    }

    LeaveCriticalSection(&clients_mutex);
}

int add_client(Client *client)
{
    int i;

    EnterCriticalSection(&clients_mutex);

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].id == 0)
        {
            clients[i] = *client;

            LeaveCriticalSection(&clients_mutex);

            return 1;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    return 0;
}

/* -------------------------------------------------- */
/* TOKEN                                              */
/* -------------------------------------------------- */

void generate_token(char *buffer,
                    size_t size,
                    int client_id)
{
    snprintf(
        buffer,
        size,
        "TOKEN_%d_%lld",
        client_id,
        (long long)time(NULL));
}

/* -------------------------------------------------- */
/* LOGIN                                              */
/* -------------------------------------------------- */

void handle_login(Client *client, char *message)
{
    char nickname[MAX_NICK_LEN];

    memset(nickname, 0, sizeof(nickname));

    if (sscanf(message, "LOGIN|%31s", nickname) != 1)
    {
        SSL_write(
            client->ssl,
            "ERROR|Invalid login format",
            26);

        return;
    }

    if (strlen(nickname) == 0)
    {
        SSL_write(
            client->ssl,
            "ERROR|Empty nickname",
            20);

        return;
    }

    if (nickname_exists(nickname))
    {
        SSL_write(
            client->ssl,
            "ERROR|Nickname already used",
            27);

        return;
    }

    strcpy(client->nickname, nickname);

    generate_token(
        client->token,
        sizeof(client->token),
        client->id);

    client->state = STATE_AUTHENTICATED;

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == client->id)
        {
            strcpy(clients[i].nickname,
                client->nickname);

            strcpy(clients[i].token,
                client->token);

            clients[i].state =
                STATE_AUTHENTICATED;

            break;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    printf(
        "[INFO] User logged in: %s\n",
        client->nickname);

    char response[MAX_MSG_LEN];

    snprintf(
        response,
        sizeof(response),
        "LOGIN_OK|%s",
        client->token);

    SSL_write(
        client->ssl,
        response,
        (int)strlen(response));
}

/* -------------------------------------------------- */
/* LOBBY                                     */
/* -------------------------------------------------- */

int create_lobby(const char *name, int host_id)
{
    int i;

    EnterCriticalSection(&lobbies_mutex);

    for(i = 0; i < MAX_LOBBIES; i++)
    {
        if(!lobbies[i].active)
        {
            lobbies[i].id = next_lobby_id++;
            lobbies[i].active = 1;

            strcpy(lobbies[i].name, name);

            lobbies[i].host_id = host_id;

            lobbies[i].player_count = 1;

            LeaveCriticalSection(&lobbies_mutex);

            return lobbies[i].id;
        }
    }

    LeaveCriticalSection(&lobbies_mutex);

    return -1;
}

int find_lobby(const char *name)
{
    int i;

    for(i = 0; i < MAX_LOBBIES; i++)
    {
        if(!lobbies[i].active)
            continue;

        if(strcmp(lobbies[i].name, name) == 0)
            return i;
    }

    return -1;
}

void handle_create_lobby(Client *client, char *message)
{
    char lobby_name[MAX_LOBBY_NAME];

    if(sscanf(message,
              "CREATE_LOBBY|%63s",
              lobby_name) != 1)
    {
        SSL_write(client->ssl,
                  "ERROR|Invalid lobby name",
                  24);
        return;
    }

    if(client->state != STATE_AUTHENTICATED)
    {
        SSL_write(client->ssl,
                  "ERROR|Login required",
                  20);
        return;
    }

    int lobby_id =
        create_lobby(lobby_name,
                     client->id);

    if(lobby_id < 0)
    {
        SSL_write(client->ssl,
                  "ERROR|Cannot create lobby",
                  25);
        return;
    }

    client->lobby_id = lobby_id;
    client->state = STATE_IN_LOBBY;

    char response[MAX_MSG_LEN];

    snprintf(response,
             sizeof(response),
             "LOBBY_CREATED|%d",
             lobby_id);

    SSL_write(client->ssl,
              response,
              (int)strlen(response));

    printf("[LOBBY] %s created lobby %s\n",
           client->nickname,
           lobby_name);
}

void handle_join(Client *client,
                 char *message)
{
    char lobby_name[MAX_LOBBY_NAME];

    if(sscanf(message,
              "JOIN|%63s",
              lobby_name) != 1)
    {
        SSL_write(client->ssl,
                  "ERROR|Invalid join",
                  18);
        return;
    }

    int idx = find_lobby(lobby_name);

    if(idx < 0)
    {
        SSL_write(client->ssl,
                  "ERROR|Lobby not found",
                  21);
        return;
    }

    lobbies[idx].player_count++;

    client->lobby_id =
        lobbies[idx].id;

    client->state =
        STATE_IN_LOBBY;

    char response[MAX_MSG_LEN];

    snprintf(response,
             sizeof(response),
             "JOIN_OK|%s",
             lobby_name);

    SSL_write(client->ssl,
              response,
              (int)strlen(response));

    printf("[LOBBY] %s joined %s\n",
           client->nickname,
           lobby_name);
}

void handle_leave(Client *client)
{
    if(client->state != STATE_IN_LOBBY)
    {
        SSL_write(client->ssl,
                  "ERROR|Not in lobby",
                  18);
        return;
    }

    int i;

    for(i = 0; i < MAX_LOBBIES; i++)
    {
        if(lobbies[i].id ==
           client->lobby_id)
        {
            lobbies[i].player_count--;

            if(lobbies[i].player_count <= 0)
            {
                lobbies[i].active = 0;

                printf(
                    "[LOBBY] Lobby %s removed\n",
                    lobbies[i].name);
            }

            break;
        }
    }

    client->lobby_id = -1;

    client->state =
        STATE_AUTHENTICATED;

    SSL_write(client->ssl,
              "LEAVE_OK",
              8);
}

/* -------------------------------------------------- */
/* CLIENT THREAD                                      */
/* -------------------------------------------------- */

DWORD WINAPI client_thread(LPVOID arg)
{
    Client *client = (Client *)arg;

    char buffer[MAX_MSG_LEN];

    printf(
        "[INFO] Client connected (id=%d)\n",
        client->id);

    if (SSL_accept(client->ssl) <= 0)
    {
        ERR_print_errors_fp(stderr);

        SSL_free(client->ssl);

        closesocket(client->socket_fd);

        remove_client(client->id);

        free(client);

        return 0;
    }

    printf(
        "[TLS] Handshake successful (client id=%d)\n",
        client->id);

    while (1)
    {
        int bytes;

        memset(buffer, 0, sizeof(buffer));

        bytes = SSL_read(
            client->ssl,
            buffer,
            sizeof(buffer) - 1);

        if (bytes <= 0)
            break;

        buffer[bytes] = '\0';

        printf(
            "[RECV][%d] %s\n",
            client->id,
            buffer);

        switch (get_message_type(buffer))
        {
            case MSG_LOGIN:
                handle_login(client, buffer);
                break;

            case MSG_CREATE_LOBBY:
                handle_create_lobby(client, buffer);
                break;

            case MSG_JOIN:
                handle_join(client, buffer);
                break;

            case MSG_LEAVE:
                handle_leave(client);
                break;

            default:
                SSL_write(
                    client->ssl,
                    "ERROR|Unknown command",
                    21);
                break;
        }
    }

    printf(
        "[INFO] Client disconnected (id=%d)\n",
        client->id);

    SSL_shutdown(client->ssl);

    SSL_free(client->ssl);

    closesocket(client->socket_fd);

    remove_client(client->id);

    free(client);

    return 0;
}

/* -------------------------------------------------- */
/* MAIN                                               */
/* -------------------------------------------------- */

int main(int argc, char *argv[])
{
    WSADATA wsa;

    SOCKET listen_socket;

    struct sockaddr_in server_addr;

    SSL_CTX *ctx;

    int port;

    if (argc != 2)
    {
        printf(
            "Usage: %s <port>\n",
            argv[0]);

        return EXIT_FAILURE;
    }

    port = atoi(argv[1]);

    InitializeCriticalSection(&clients_mutex);

    memset(clients, 0, sizeof(clients));

    InitializeCriticalSection(&lobbies_mutex);

    memset(lobbies, 0, sizeof(lobbies));

    if (WSAStartup(
            MAKEWORD(2, 2),
            &wsa) != 0)
    {
        fprintf(stderr,
                "WSAStartup failed\n");

        return EXIT_FAILURE;
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    ctx = create_server_context();

    configure_server_context(ctx);

    listen_socket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP);

    if (listen_socket == INVALID_SOCKET)
    {
        fprintf(stderr,
                "socket() failed\n");

        return EXIT_FAILURE;
    }

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);

    server_addr.sin_port =
        htons(port);

    if (bind(
            listen_socket,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == SOCKET_ERROR)
    {
        fprintf(stderr,
                "bind() failed\n");

        return EXIT_FAILURE;
    }

    if (listen(
            listen_socket,
            SOMAXCONN) == SOCKET_ERROR)
    {
        fprintf(stderr,
                "listen() failed\n");

        return EXIT_FAILURE;
    }

    printf(
        "[SMLP] Server listening on port %d\n",
        port);

    while (1)
    {
        SOCKET client_socket;

        struct sockaddr_in client_addr;

        int client_len =
            sizeof(client_addr);

        client_socket = accept(
            listen_socket,
            (struct sockaddr *)&client_addr,
            &client_len);

        if (client_socket ==
            INVALID_SOCKET)
        {
            continue;
        }

        Client *client =
            (Client *)malloc(
                sizeof(Client));

        memset(
            client,
            0,
            sizeof(Client));

        client->id =
            next_client_id++;

        client->socket_fd =
            client_socket;

        client->state =
            STATE_CONNECTED;

        client->lobby_id = -1;

        client->ssl =
            SSL_new(ctx);

        SSL_set_fd(
            client->ssl,
            (int)client_socket);

        add_client(client);

        HANDLE thread =
            CreateThread(
                NULL,
                0,
                client_thread,
                client,
                0,
                NULL);

        if (thread != NULL)
            CloseHandle(thread);
    }

    closesocket(listen_socket);

    SSL_CTX_free(ctx);

    EVP_cleanup();

    WSACleanup();

    DeleteCriticalSection(
        &clients_mutex);

    DeleteCriticalSection(
        &lobbies_mutex);

    return EXIT_SUCCESS;
}