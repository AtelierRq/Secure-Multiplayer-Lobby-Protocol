#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_MSG_LEN 1024
#define MAX_NICK_LEN 32
#define MAX_TOKEN_LEN 64

#define MAX_CLIENTS 100
#define MAX_LOBBIES 50
#define MAX_LOBBY_NAME 64

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

    MSG_CREATE_LOBBY,
    MSG_LOBBY_CREATED,

    MSG_JOIN,
    MSG_JOIN_OK,

    MSG_LIST_LOBBIES,
    MSG_LOBBIES,

    MSG_LIST_PLAYERS,
    MSG_PLAYERS,

    MSG_LEAVE,
    MSG_LEAVE_OK,

    MSG_CHAT,
    MSG_CHAT_BROADCAST,

    MSG_READY,
    MSG_READY_OK,

    MSG_START,
    MSG_GAME_STARTED,

    MSG_END_GAME,
    MSG_GAME_ENDED,

    MSG_PING,
    MSG_PONG,

    MSG_BYE,

    MSG_ERROR,

    MSG_UNKNOWN

} MessageType;

MessageType get_message_type(const char *msg);

const char *message_type_to_string(MessageType type);

#endif