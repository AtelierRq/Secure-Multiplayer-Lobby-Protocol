#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "../common/protocol.h"

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

void perform_login(SSL *ssl)
{
    char nickname[MAX_NICK_LEN];

    char message[MAX_MSG_LEN];

    char response[MAX_MSG_LEN];

    printf(
        "Nickname: ");

    fgets(
        nickname,
        sizeof(nickname),
        stdin);

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

    if (bytes <= 0)
    {
        printf(
            "Connection lost\n");

        return;
    }

    response[bytes] = '\0';

    printf(
        "\n[SMLP] Server response:\n%s\n",
        response);

    if (get_message_type(response)
        == MSG_LOGIN_OK)
    {
        printf(
            "\n[SMLP] Login successful\n");
    }
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

    perform_login(ssl);

    printf(
        "\nPress ENTER to disconnect...");

    getchar();

    SSL_shutdown(ssl);

    SSL_free(ssl);

    closesocket(sockfd);

    SSL_CTX_free(ctx);

    EVP_cleanup();

    WSACleanup();

    return EXIT_SUCCESS;
}