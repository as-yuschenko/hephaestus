#include "commands.h"
#include <cstring>
#include <cstdlib>

command cmd;

int cmd_parse(char* buff)
{
    memset(&cmd, 0xFF, sizeof(command));

    char* pch;
    int res = -1;

    pch = strtok(buff, "-");
    while (pch != nullptr)
    {
        res++;
        switch (res)
        {
        case 0:
            cmd.client = (unsigned char)strtoul(pch, NULL, 10);
            if (cmd.client > 0x03) return -1;
            break;
        case 1:
            cmd.type = (unsigned char)strtoul(pch, NULL, 10);
            if (cmd.type > 0x0A) return -1;
            break;
        case 2:
            cmd.addr = (unsigned char)strtoul(pch, NULL, 10);
            if (cmd.addr > 127) return -1;
            break;
        case 3:
            cmd.num = (unsigned char)strtoul(pch, NULL, 10);
            if (cmd.num == 0) return -1;
            break;

        default:
            break;
        }
        pch = strtok(nullptr, "-");
    }

    if (res != 3) return -1;
    return 0;
};
