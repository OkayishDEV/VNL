#pragma once
#include <sys/socket.h>

struct sockaddr_un {
    unsigned short sun_family;
    char           sun_path[108];
};
