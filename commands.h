#ifndef COMMANDS_H_INCLUDED
#define COMMANDS_H_INCLUDED



#define CMD_TYPE_Z_ARM          0x00
#define CMD_TYPE_Z_DISARM       0x01
#define CMD_TYPE_Z_ADC          0x02
#define CMD_TYPE_Z_CNTR         0x03
#define CMD_TYPE_Z_STATE        0x04
#define CMD_TYPE_P_ARM          0x05
#define CMD_TYPE_P_DISARM       0x06
#define CMD_TYPE_P_STATE        0x07
#define CMD_TYPE_R_ON           0x08
#define CMD_TYPE_R_OFF          0x09
#define CMD_TYPE_R_STATE        0x0A

#define CMD_CLIENT_CLI          0x00
#define CMD_CLIENT_WEB          0x01
#define CMD_CLIENT_TG           0x02
#define CMD_CLIENT_GSM          0x03

struct command
{
    unsigned char type;
    unsigned char client;
    unsigned char addr; //dev addr or 0 if part
    unsigned char num; //zone, part or relay
};

extern command cmd;

int cmd_parse(char* buff);
#endif // COMMANDS_H_INCLUDED
