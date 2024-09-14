#include "devices.h"

#include "../lib/Ceres/Ceres.h"

device* init_device (device* dev, unsigned char _addr_o, unsigned char _type)
{
    dev->addr_o = _addr_o;
    dev->addr_s = _addr_o ^ 0x80;
    dev->g_key = ceres_msg_keygen();
    dev->type = _type;

    for (int i = 0; i < CERES_SIZE_DEV_NAMES_ARR; i++)
    {
        if (CERES_DEV_TYPE[i][0] == _type)
        {
           dev->num_zones = CERES_DEV_TYPE[i][1];
           dev->num_relays = CERES_DEV_TYPE[i][2];
        }
    }

    return dev;
};
