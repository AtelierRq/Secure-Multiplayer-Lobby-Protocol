#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "../common/protocol.h"

SSL *g_ssl = NULL;

char g_nickname[MAX_NICK_LEN] = "";

char g_lobby[MAX_LOBBY_NAME] = "";

int g_ready = 0;

int g_in_game = 0;

volatile int g_running = 1;

// #pragma comment(lib, "ws2_32.lib")

/* -------------------------------------------------- */
/* TLS                                                */
/* -------------------------------------------------- */

SSL_CTX *create_client_context(void)
{
    SSL_CTX *ctx;

    ctx = SSL_CTX_new(TLS_client_method());

    if (!ctx)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_client_context(SSL_CTX *ctx)
{
    if (!SSL_CTX_load_verify_locations(
            ctx,
            "certs/ca_cert.pem",
            NULL))
    {
        fprintf(stderr,
                "Cannot load CA certificate\n");

        ERR_print_errors_fp(stderr);

        exit(EXIT_FAILURE);
    }

    SSL_CTX_set_verify(
        ctx,
        SSL_VERIFY_PEER,
        NULL);
}

void show_server_certificate(SSL *ssl)
{
    X509 *cert;
    char *line;

    cert = SSL_get_peer_certificate(ssl);

    if (cert == NULL)
    {
        printf(
            "[TLS] No server certificate\n");

        return;
    }

    printf(
        "\n[TLS] Server certificate:\n");

    line =
        X509_NAME_oneline(
            X509_get_subject_name(cert),
            NULL,
            0);

    printf(
        "Subject: %s\n",
        line);

    OPENSSL_free(line);

    line =
        X509_NAME_oneline(
            X509_get_issuer_name(cert),
            NULL,
            0);

    printf(
        "Issuer : %s\n",
        line);

    OPENSSL_free(line);

    X509_free(cert);
}

/* -------------------------------------------------- */
/* LOGIN                                              */
/* -------------------------------------------------- */

int perform_login(SSL *ssl, char *current_nickname)
{
    char nickname[MAX_NICK_LEN];

    char message[MAX_MSG_LEN];

    char response[MAX_MSG_LEN];

    while(1)
    {
        printf("Nickname: ");

        if(fgets(nickname,
                 sizeof(nickname),
                 stdin) == NULL)
        {
            return 0;
        }

        nickname[strcspn(
            nickname,
            "\r\n")] = '\0';

        snprintf(
            message,
            sizeof(message),
            "LOGIN|%s",
            nickname);

        SSL_write(
            ssl,
            message,
            (int)strlen(message));

        memset(
            response,
            0,
            sizeof(response));

        int bytes =
            SSL_read(
                ssl,
                response,
                sizeof(response) - 1);

        if(bytes <= 0)
        {
            printf("Connection lost\n");
            return 0;
        }

        response[bytes] = '\0';

        printf(
            "\n[SMLP] Server response:\n%s\n",
            response);

        if(get_message_type(response) == MSG_LOGIN_OK)
        {
            strcpy(current_nickname, nickname);

            printf(
                "\n[SMLP] Login successful\n\n");
            return 1;
        }

        printf(
            "\nNickname is unavailable. Try again.\n\n");
    }
}

void print_prompt(void)
{
    if(g_in_game)
    {
        printf(
            "\nSMLP[%s|%s|IN_GAME]> ",
            g_nickname,
            g_lobby);
    }
    else if(strlen(g_lobby) > 0 &&
            g_ready)
    {
        printf(
            "\nSMLP[%s|%s|READY]> ",
            g_nickname,
            g_lobby);
    }
    else if(strlen(g_lobby) > 0)
    {
        printf(
            "\nSMLP[%s|%s]> ",
            g_nickname,
            g_lobby);
    }
    else
    {
        printf(
            "\nSMLP[%s]> ",
            g_nickname);
    }

    fflush(stdout);
}

DWORD WINAPI receiver_thread(LPVOID arg)
{
    (void)arg;

    char response[MAX_MSG_LEN];

    while(g_running)
    {
        int bytes =
            SSL_read(
                g_ssl,
                response,
                sizeof(response)-1);

        if(bytes <= 0)
        {
            printf(
                "\nConnection lost\n");

            g_running = 0;

            break;
        }

        response[bytes] = '\0';

        if(strncmp(response,
                   "JOIN_OK|",
                   8) == 0)
        {
            strcpy(
                g_lobby,
                response + 8);
        }

        if(strncmp(response,
                   "LOBBY_CREATED|",
                   14) == 0)
        {
            char *last_pipe =
                strrchr(response,'|');

            if(last_pipe)
            {
                strcpy(
                    g_lobby,
                    last_pipe + 1);
            }
        }

        if(strcmp(response,
                  "LEAVE_OK") == 0)
        {
            g_lobby[0] = '\0';
            g_ready = 0;
            g_in_game = 0;
        }

        if(strcmp(response,
                  "READY_OK") == 0)
        {
            g_ready = 1;
        }

        if(strcmp(response,
                  "GAME_STARTED") == 0)
        {
            g_in_game = 1;
            g_ready = 0;
        }

        if(strcmp(response,
                  "GAME_ENDED") == 0)
        {
            g_in_game = 0;
            g_ready = 0;
        }

        if(strncmp(response,
                   "CHAT_MSG|",
                   9) == 0)
        {
            char sender[64];
            char text[MAX_MSG_LEN];

            sscanf(
                response,
                "CHAT_MSG|%63[^|]|%511[^\n]",
                sender,
                text);

            printf(
                "\n[%s]: %s\n",
                sender,
                text);

            print_prompt();

            continue;
        }

        printf(
            "\nSERVER: %s\n",
            response);

        print_prompt();
    }

    return 0;
}

/* -------------------------------------------------- */
/* MAIN                                               */
/* -------------------------------------------------- */

int main(int argc, char *argv[])
{
    WSADATA wsa;

    SOCKET sockfd;

    struct sockaddr_in server_addr;

    SSL_CTX *ctx;

    SSL *ssl;

    const char *ip;

    int port;

    char current_nickname[MAX_NICK_LEN] = "";

    if (argc != 3)
    {
        printf(
            "Usage: %s <ip> <port>\n",
            argv[0]);

        return EXIT_FAILURE;
    }

    ip = argv[1];

    port = atoi(argv[2]);

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

    ctx =
        create_client_context();

    configure_client_context(ctx);

    sockfd =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP);

    if (sockfd == INVALID_SOCKET)
    {
        fprintf(stderr,
                "socket() failed\n");

        return EXIT_FAILURE;
    }

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(port);

    if (inet_pton(
            AF_INET,
            ip,
            &server_addr.sin_addr)
        <= 0)
    {
        fprintf(stderr,
                "Invalid address\n");

        return EXIT_FAILURE;
    }

    if (connect(
            sockfd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr))
        == SOCKET_ERROR)
    {
        fprintf(stderr,
                "connect() failed\n");

        return EXIT_FAILURE;
    }

    ssl =
        SSL_new(ctx);

    SSL_set_fd(
        ssl,
        (int)sockfd);

    if (SSL_connect(ssl) <= 0)
    {
        ERR_print_errors_fp(stderr);

        return EXIT_FAILURE;
    }

    printf(
        "\n[TLS] Handshake successful\n");

    if (SSL_get_verify_result(ssl)
        == X509_V_OK)
    {
        printf(
            "[TLS] Certificate verification successful\n");
    }
    else
    {
        printf(
            "[TLS] Certificate verification FAILED\n");

        return EXIT_FAILURE;
    }

    show_server_certificate(ssl);

    if(!perform_login(ssl, current_nickname))
    {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(sockfd);

        SSL_CTX_free(ctx);

        WSACleanup();

        return EXIT_FAILURE;
    }

    strcpy(g_nickname, current_nickname);

    g_ssl = ssl;

    HANDLE recv_thread =
    CreateThread(
        NULL,
        0,
        receiver_thread,
        NULL,
        0,
        NULL);
    
    char command[MAX_MSG_LEN];

    while(g_running)
    {
        print_prompt();

        if(fgets(
                command,
                sizeof(command),
                stdin) == NULL)
        {
            break;
        }

        command[strcspn(
            command,
            "\r\n")] = '\0';

        if(strcmp(
                command,
                "exit") == 0)
        {
            break;
        }

        SSL_write(
            ssl,
            command,
            (int)strlen(command));
    }

    g_running = 0;

    WaitForSingleObject(recv_thread, INFINITE);

    CloseHandle(recv_thread);
        
    SSL_shutdown(ssl);

    SSL_free(ssl);

    closesocket(sockfd);

    SSL_CTX_free(ctx);

    EVP_cleanup();

    WSACleanup();

    return EXIT_SUCCESS;
}