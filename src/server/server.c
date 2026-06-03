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

#include <time.h>

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
void write_log(const char *message)
{
    FILE *f =
        fopen("logs/server.log", "a");

    if(f == NULL)
        return;

    time_t now =
        time(NULL);

    struct tm *tm_info =
        localtime(&now);

    fprintf(
        f,
        "[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
        tm_info->tm_year + 1900,
        tm_info->tm_mon + 1,
        tm_info->tm_mday,
        tm_info->tm_hour,
        tm_info->tm_min,
        tm_info->tm_sec,
        message);

    fclose(f);
}

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
            int lobby_id = clients[i].lobby_id;

            if(lobby_id != -1)
            {
                for(int j = 0;
                    j < MAX_LOBBIES;
                    j++)
                {
                    if(lobbies[j].id ==
                    lobby_id)
                    {
                        lobbies[j].player_count--;

                        if(lobbies[j].player_count <= 0)
                        {
                            lobbies[j].active = 0;

                            printf(
                                "[LOBBY] Lobby %s removed\n",
                                lobbies[j].name);
                        }

                        break;
                    }
                }
            }

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
    client->ready = 0;

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

    char log_msg[256];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "LOGIN: %s",
        client->nickname);

    write_log(log_msg);

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
    client->ready = 0;

    char lobby_name[MAX_LOBBY_NAME];

    if(client->state == STATE_IN_LOBBY)
    {
        SSL_write(
            client->ssl,
            "ERROR|Already in lobby",
            22);

        return;
    }

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

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == client->id)
        {
            clients[i].lobby_id = lobby_id;
            clients[i].state = STATE_IN_LOBBY;
            break;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    char response[MAX_MSG_LEN];

    snprintf(response,
         sizeof(response),
         "LOBBY_CREATED|%d|%s",
         lobby_id,
         lobby_name);

    SSL_write(client->ssl,
              response,
              (int)strlen(response));

    printf("[LOBBY] %s created lobby %s\n",
           client->nickname,
           lobby_name);

    char log_msg[256];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "LOBBY_CREATED: %s by %s",
        lobby_name,
        client->nickname);

    write_log(log_msg);
}

void handle_join(Client *client, char *message)
{
    client->ready = 0;

    char lobby_name[MAX_LOBBY_NAME];

    if(client->state == STATE_IN_LOBBY)
    {
        SSL_write(
            client->ssl,
            "ERROR|Already in lobby",
            22);

        return;
    }

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

    client->lobby_id = lobbies[idx].id;
    client->state = STATE_IN_LOBBY;

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == client->id)
        {
            clients[i].lobby_id =
                lobbies[idx].id;

            clients[i].state =
                STATE_IN_LOBBY;

            break;
        }
    }

    LeaveCriticalSection(&clients_mutex);

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

    char log_msg[256];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "JOIN: %s -> %s",
        client->nickname,
        lobby_name);

    write_log(log_msg);

}

void handle_list_lobbies(Client *client)
{
    char response[MAX_MSG_LEN];

    strcpy(response, "LOBBIES|");

    EnterCriticalSection(&lobbies_mutex);

    for(int i = 0; i < MAX_LOBBIES; i++)
    {
        if(!lobbies[i].active)
            continue;

        char temp[128];

        snprintf(temp,
                 sizeof(temp),
                 "%s(%d),",
                 lobbies[i].name,
                 lobbies[i].player_count);

        strcat(response, temp);
    }

    LeaveCriticalSection(&lobbies_mutex);

    SSL_write(
        client->ssl,
        response,
        (int)strlen(response));
}

void handle_list_players(Client *client)
{
    int host_id = -1;

    char response[MAX_MSG_LEN];

    strcpy(response, "PLAYERS|");

    if(client->lobby_id < 0)
    {
        SSL_write(
            client->ssl,
            "ERROR|Not in lobby",
            18);

        return;
    }

    for(int i = 0; i < MAX_LOBBIES; i++)
    {
        if(lobbies[i].id ==
        client->lobby_id)
        {
            host_id =
                lobbies[i].host_id;

            break;
        }
    }

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id !=
           client->lobby_id)
            continue;

        char temp[128];

        if(clients[i].id ==
        host_id)
        {
            snprintf(
                temp,
                sizeof(temp),
                "%s[Host],",
                clients[i].nickname);
        }
        else
        {
            snprintf(
                temp,
                sizeof(temp),
                "%s,",
                clients[i].nickname);
        }

        strcat(response,
            temp);
    }

    LeaveCriticalSection(&clients_mutex);

    SSL_write(
        client->ssl,
        response,
        (int)strlen(response));
}

int is_host(Client *client);

void handle_leave(Client *client)
{
    client->ready = 0;

    if(client->state != STATE_IN_LOBBY)
    {
        SSL_write(client->ssl,
                  "ERROR|Not in lobby",
                  18);
        return;
    }

    if(is_host(client))
    {
        int lobby_id = client->lobby_id;

        EnterCriticalSection(&clients_mutex);

        for(int j = 0; j < MAX_CLIENTS; j++)
        {
            if(clients[j].id == 0)
                continue;

            if(clients[j].lobby_id != lobby_id)
                continue;

            clients[j].lobby_id = -1;
            clients[j].ready = 0;
            clients[j].state = STATE_AUTHENTICATED;

            SSL_write(
                clients[j].ssl,
                "LEAVE_OK",
                8);
        }

        LeaveCriticalSection(&clients_mutex);

        for(int j = 0; j < MAX_LOBBIES; j++)
        {
            if(lobbies[j].id == lobby_id)
            {
                lobbies[j].player_count = 0;
                lobbies[j].active = 0;

                printf(
                    "[LOBBY] Lobby %s removed by host\n",
                    lobbies[j].name);

                break;
            }
        }

        return;
    }

    char log_msg[256];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "LEAVE: %s",
        client->nickname);

    write_log(log_msg);

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
    client->state = STATE_AUTHENTICATED;

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == client->id)
        {
            clients[i].lobby_id = -1;

            clients[i].state =
                STATE_AUTHENTICATED;

            break;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    SSL_write(client->ssl, "LEAVE_OK", 8);

    snprintf(
        log_msg,
        sizeof(log_msg),
        "LEAVE: %s",
        client->nickname);

    write_log(log_msg);
}

void handle_ready(Client *client)
{

    if(client->state != STATE_IN_LOBBY)
    {
        SSL_write(
            client->ssl,
            "ERROR|Not in lobby",
            18);

        return;
    }

    client->ready = 1;

    char log_msg[256];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "READY: %s",
        client->nickname);

    write_log(log_msg);

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == client->id)
        {
            clients[i].ready = 1;
            break;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    SSL_write(
        client->ssl,
        "READY_OK",
        8);

    printf(
        "[READY] %s is ready\n",
        client->nickname);
}

int is_host(Client *client)
{
    for(int i = 0; i < MAX_LOBBIES; i++)
    {
        if(lobbies[i].id ==
           client->lobby_id)
        {
            return
                lobbies[i].host_id ==
                client->id;
        }
    }

    return 0;
}

int all_players_ready(int lobby_id)
{
    int players = 0;

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id != lobby_id)
            continue;

        players++;

        if(clients[i].ready == 0)
            return 0;
    }

    return players >= 2;
}

void handle_start(Client *client)
{
    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id ==
        client->lobby_id)
        {
            clients[i].state =
                STATE_IN_GAME;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    client->state = STATE_IN_GAME;

    if(!is_host(client))
    {
        SSL_write(
            client->ssl,
            "ERROR|Only host can start game",
            31);

        return;
    }

    if(!all_players_ready(
            client->lobby_id))
    {
        SSL_write(
            client->ssl,
            "ERROR|Not all players are ready",
            32);

        return;
    }

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id ==
        client->lobby_id)
        {
            SSL_write(
                clients[i].ssl,
                "GAME_STARTED",
                12);
        }

        char log_msg[256];

        snprintf(
            log_msg,
            sizeof(log_msg),
            "GAME_STARTED by %s",
            client->nickname);

        write_log(log_msg);
    }

    printf(
        "[GAME] Lobby %d started\n",
        client->lobby_id);
}

void handle_end_game(Client *client)
{
    if(!is_host(client))
    {
        SSL_write(
            client->ssl,
            "ERROR|Only host can end game",
            29);

        return;
    }

    if(client->state !=
       STATE_IN_GAME)
    {
        SSL_write(
            client->ssl,
            "ERROR|Game not started",
            22);

        return;
    }

    EnterCriticalSection(&clients_mutex);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id ==
           client->lobby_id)
        {
            clients[i].state =
                STATE_IN_LOBBY;

            clients[i].ready = 0;
        }
    }

    LeaveCriticalSection(&clients_mutex);

    client->state =
        STATE_IN_LOBBY;

    client->ready = 0;

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id ==
        client->lobby_id)
        {
            SSL_write(
                clients[i].ssl,
                "GAME_ENDED",
                10);
        }

        char log_msg[256];

        snprintf(
            log_msg,
            sizeof(log_msg),
            "GAME_ENDED by %s",
            client->nickname);

        write_log(log_msg);
    }

    printf(
        "[GAME] Lobby %d ended\n",
        client->lobby_id);
}

void broadcast_to_lobby(int lobby_id, const char *message)
{
    EnterCriticalSection(
        &clients_mutex);

    for(int i = 0;
        i < MAX_CLIENTS;
        i++)
    {
        if(clients[i].id == 0)
            continue;

        if(clients[i].lobby_id !=
           lobby_id)
            continue;

        SSL_write(
            clients[i].ssl,
            message,
            (int)strlen(message));
    }

    LeaveCriticalSection(
        &clients_mutex);
}

void handle_chat(Client *client, char *message)
{
    char *text;

    char response[MAX_MSG_LEN];

    if(client->lobby_id < 0)
    {
        SSL_write(
            client->ssl,
            "ERROR|Not in lobby",
            18);

        return;
    }

    text =
        strchr(message, '|');

    if(text == NULL)
    {
        SSL_write(
            client->ssl,
            "ERROR|Invalid chat",
            18);

        return;
    }

    text++;

    snprintf(
        response,
        sizeof(response),
        "CHAT_MSG|%s|%s",
        client->nickname,
        text);

    broadcast_to_lobby(
        client->lobby_id,
        response);

    printf(
        "[CHAT][%s] %s\n",
        client->nickname,
        text);

    char log_msg[512];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "CHAT: %s -> %s",
        client->nickname,
        text);

    write_log(log_msg);
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

        client->last_activity = time(NULL);

        EnterCriticalSection(&clients_mutex);

        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            if(clients[i].id == client->id)
            {
                clients[i].last_activity =
                    client->last_activity;

                break;
            }
        }

        LeaveCriticalSection(&clients_mutex);

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

            case MSG_LIST_LOBBIES:
                handle_list_lobbies(client);
                break;

            case MSG_LIST_PLAYERS:
                handle_list_players(client);
                break;

            case MSG_JOIN:
                handle_join(client, buffer);
                break;

            case MSG_LEAVE:
                handle_leave(client);
                break;

            case MSG_READY:
                handle_ready(client);
                break;

            case MSG_START:
                handle_start(client);
                break;

            case MSG_END_GAME:
                handle_end_game(client);
                break;

            case MSG_CHAT:
                handle_chat(client, buffer);
                break;

            default:
                SSL_write(
                    client->ssl,
                    "ERROR|Unknown command",
                    21);
                break;
        }
    }

    printf("[INFO] Client disconnected (id=%d)\n", client->id);

    char log_msg[256];

    snprintf(
        log_msg,
        sizeof(log_msg),
        "DISCONNECT: %s",
        client->nickname);

    write_log(log_msg);

    SSL_shutdown(client->ssl);

    SSL_free(client->ssl);

    closesocket(client->socket_fd);

    remove_client(client->id);

    free(client);

    return 0;
}

DWORD WINAPI timeout_thread(LPVOID arg)
{
    (void)arg;

    while(1)
    {
        Sleep(10000);

        time_t now =
            time(NULL);

        EnterCriticalSection(
            &clients_mutex);

        for(int i = 0;
            i < MAX_CLIENTS;
            i++)
        {
            if(clients[i].id == 0)
                continue;

            double diff =
                difftime(
                    now,
                    clients[i].last_activity);

            if(diff > 120)
            {
                printf(
                    "[TIMEOUT] %s disconnected\n",
                    clients[i].nickname);

                shutdown(
                    clients[i].socket_fd,
                    SD_BOTH);

                char log_msg[256];

                snprintf(
                    log_msg,
                    sizeof(log_msg),
                    "TIMEOUT: %s",
                    clients[i].nickname);

                write_log(log_msg);
            }
        }

        LeaveCriticalSection(
            &clients_mutex);
    }

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

    CreateThread(NULL, 0, timeout_thread, NULL, 0, NULL);

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

        client->last_activity = time(NULL);

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