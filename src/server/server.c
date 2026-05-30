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

CRITICAL_SECTION clients_mutex;

static int next_client_id = 1;

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

    return EXIT_SUCCESS;
}