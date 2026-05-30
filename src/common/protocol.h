#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_MSG_LEN 1024
#define MAX_NICK_LEN 32
#define MAX_TOKEN_LEN 64

#define MAX_CLIENTS 100
#define MAX_LOBBIES 50

typedef enum
{
    STATE_CONNECTED,
    STATE_AUTHENTICATED,
    STATE_IN_LOBBY,
    STATE_READY,
    STATE_IN_GAME
} ClientState;

typedef enum
{
    MSG_LOGIN,
    MSG_LOGIN_OK,
    MSG_ERROR,

    MSG_CREATE_LOBBY,
    MSG_JOIN,
    MSG_LEAVE,

    MSG_CHAT,

    MSG_READY,
    MSG_START,

    MSG_PING,
    MSG_PONG,

    MSG_BYE,

    MSG_UNKNOWN
} MessageType;

MessageType get_message_type(const char *msg);

const char *message_type_to_string(MessageType type);

#endif