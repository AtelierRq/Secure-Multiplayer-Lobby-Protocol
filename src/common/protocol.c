#include <string.h>

#include "protocol.h"

MessageType get_message_type(const char *msg)
{
    if (strncmp(msg, "LOGIN|", 6) == 0)
        return MSG_LOGIN;

    if (strncmp(msg, "LOGIN_OK|", 9) == 0)
        return MSG_LOGIN_OK;

    if (strncmp(msg, "ERROR|", 6) == 0)
        return MSG_ERROR;

    if (strncmp(msg, "CREATE_LOBBY|", 13) == 0)
        return MSG_CREATE_LOBBY;

    if (strncmp(msg, "LOBBY_CREATED|", 14) == 0)
        return MSG_LOBBY_CREATED;

    if (strncmp(msg, "JOIN|", 5) == 0)
        return MSG_JOIN;

    if (strncmp(msg, "JOIN_OK|", 8) == 0)
        return MSG_JOIN_OK;

    if (strcmp(msg, "LIST_LOBBIES") == 0)
        return MSG_LIST_LOBBIES;

    if (strncmp(msg, "LOBBIES|", 8) == 0)
        return MSG_LOBBIES;

    if (strcmp(msg, "LIST_PLAYERS") == 0)
        return MSG_LIST_PLAYERS;

    if (strncmp(msg, "PLAYERS|", 8) == 0)
        return MSG_PLAYERS;

    if (strncmp(msg, "LEAVE", 5) == 0)
        return MSG_LEAVE;

    if (strncmp(msg, "LEAVE_OK", 8) == 0)
        return MSG_LEAVE_OK;

    if (strncmp(msg, "CHAT|", 5) == 0)
        return MSG_CHAT;

    if(strcmp(msg, "READY") == 0)
        return MSG_READY;

    if(strcmp(msg, "READY_OK") == 0)
        return MSG_READY_OK;

    if(strcmp(msg, "START") == 0)
        return MSG_START;

    if(strcmp(msg, "GAME_STARTED") == 0)
        return MSG_GAME_STARTED;

    if(strcmp(msg, "END_GAME") == 0)
        return MSG_END_GAME;

    if(strcmp(msg, "GAME_ENDED") == 0)
        return MSG_GAME_ENDED;

    if (strncmp(msg, "PING", 4) == 0)
        return MSG_PING;

    if (strncmp(msg, "PONG", 4) == 0)
        return MSG_PONG;

    if (strncmp(msg, "BYE", 3) == 0)
        return MSG_BYE;

    return MSG_UNKNOWN;
}

const char *message_type_to_string(MessageType type)
{
    switch(type)
    {
        case MSG_LOGIN:
            return "LOGIN";

        case MSG_LOGIN_OK:
            return "LOGIN_OK";

        case MSG_ERROR:
            return "ERROR";

        case MSG_CREATE_LOBBY:
            return "CREATE_LOBBY";

        case MSG_JOIN:
            return "JOIN";

        case MSG_LEAVE:
            return "LEAVE";

        case MSG_CHAT:
            return "CHAT";

        case MSG_READY:
            return "READY";

        case MSG_START:
            return "START";

        case MSG_END_GAME:
            return "END_GAME";

        case MSG_GAME_ENDED:
            return "GAME_ENDED";
            
        case MSG_PING:
            return "PING";

        case MSG_PONG:
            return "PONG";

        case MSG_BYE:
            return "BYE";

        default:
            return "UNKNOWN";
    }
}