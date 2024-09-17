#ifndef DEVICES_H_INCLUDED
#define DEVICES_H_INCLUDED

#include "../lib/Ceres/Ceres.h"

#define ZT_FIRE                 0x01
#define ZT_SEC                  0x02
#define ZT_COUNTER              0x03
#define ZT_ANALOG_DBL           0x04
#define ZT_ANALOG_LI            0x05
#define ZT_TECHNOLOGICAL        0x06



struct st_zone
{
    unsigned char states_obt;
    unsigned char states_local[CERES_SIZE_STATES_ARR];
    unsigned char num;
    unsigned char type;
    unsigned char part;
    unsigned char is_polled;
    void*         p_part = nullptr;
    void*         p_device = nullptr;

};

struct device
{
    unsigned char addr_o;
    unsigned char addr_s;
    unsigned char g_key;
    unsigned char type;
    unsigned char num_zones; //specify dev
    unsigned char num_relays;//specify dev
    unsigned char* states_zones_ptr;
    unsigned char* states_self_ptr;
    unsigned char states_obt;
    unsigned char states_local[CERES_SIZE_STATES_ARR];

    unsigned char qnt_zones; //used zones
    st_zone* zones = nullptr;
};

device* init_device (device* dev, unsigned char _addr_o, unsigned char _type);

struct st_entity
{
    device* p_device;
    st_zone* p_zone;
};

struct st_part
{
    int num;
    int zones_num;
    st_entity* entities = nullptr;
    unsigned char state_local;
    int zones_polled;
};

struct st_event
{
    int type;
    unsigned char num;

    unsigned char zone_num;
    unsigned char states_obtained;
    unsigned char states_buff[CERES_SIZE_STATES_ARR];

    unsigned char relay_num;
    unsigned char relay_prog;
};

#endif // DEVICES_H_INCLUDED
