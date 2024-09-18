#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <cstdlib>

#include "devices.h"
#include "commands.h"
#include "help.h"


#include "../Debug/debug.h"
#include "../lib/Ceres/Ceres.h"
#include "../lib/Ceres/Ceres_names.h"
#include "../lib/SerialPort/SerialPort.h"
#include "../lib/UnixSocket/UnixSocket.h"
#include "../lib/vCjson/vCjson.h"

#define RUN_DIR             "/run/hephaestus/"
#define ETC_DIR             "/etc/hephaestus/"
#define FILE_SHMEM_TOK      "/run/hephaestus/shmem_tok"
#define FILE_SHMEM_SIZE     "/var/www/html/shmem/size"
#define FILE_SOCK_CMD       "/run/hephaestus/sock_cmd"
#define FILE_PID            "/run/hephaestus/pid"
#define FILE_CONF           "/etc/hephaestus/server.conf"


//#define UART_PATH           "/dev/ttyACM0"
//#define UART_PATH           "/dev/ttyBOLID"
//#define UART_SPEED          9600
#define UART_BUFF_SIZE      1000
//#define UART_RX_WAIT        10000
//#define UART_QNT_REQUESTS   1000

#define DELAY               100000

SerialPort* g_uart = nullptr;



/*DEVICES*/
char    g_num_devices = 0;
device* g_arr_devices = nullptr;

/*SHMEM*/
#define SHMEM_MAX_PARTS   255
#define SHMEM_SIZE_DEV    7 //addr, type, state1, state2, state3, num_zones, num_relays

#define SHMEM_ID_DEVS_INFO  0x31
#define SHMEM_ID_ST_ZONES   0x32
#define SHMEM_ID_ST_PARTS   0x33
#define SHMEM_ID_ADC_LI     0x34
#define SHMEM_ID_ADC_DBL    0x35

int g_shmid_devs;
int g_shmid_states;
int g_shmid_parts;
int g_shmid_adc_li;
int g_shmid_adc_dbl;

unsigned char*  g_shmem_devs = nullptr;
unsigned char*  g_shmem_states = nullptr;
unsigned char*  g_shmem_parts = nullptr;
long int*  g_shmem_adc_li = nullptr;
double*  g_shmem_adc_dbl = nullptr;


/*SOCKETS*/
#define SOCK_CMD_BUFF_SIZE      64
#define SOCK_CMD_QUE            5
UnixSocket* g_sock_cmd = nullptr;

/*DEMON*/
volatile unsigned char g_term = 0;
int g_pid_fd = 0;
unsigned char g_deamon_run = 0;
int daemon_start(const char* pid_file_path, int* pid_file_fd);
int daemon_status(const char* pid_file_path);

/*SIGNALS*/
int sig_handler_conf();
void sig_FATAL_handler(int sig);
void sig_TERM_handler(int sig);
void sig_CONT_handler(int sig);


/*ADC zones*/
int g_num_zones_adc_int = 0;
st_entity* g_arr_zones_adc_int = nullptr;

int g_num_zones_adc_dbl = 0;
st_entity* g_arr_zones_adc_dbl = nullptr;

/*PARTS*/
int g_fd_parts_states = 0;
int g_num_parts = 0;
st_part* g_arr_parts = nullptr;

int uart_poll();
int unblockRead (int fd, int sec, int usec);
void store_event_zone (unsigned char* event, unsigned char* addr, unsigned char* zone_num);
void store_event_relay (unsigned char* event, unsigned char* addr, unsigned char* relay_num, unsigned char* prog);
void free();


int showTree(vCjson* json, int lvl)
{
    while (1)
    {
        if(json->go_next_sibling()) break;
        for (int i = 0; i < lvl; i++) printf("\t");
        json->show_node();

        while (1)
        {
            if(json->go_node_child()) break;
            showTree (json, lvl + 1);
            json->go_node_parent();
            break;
        }
    }
    return 0;
}


int main(int argc, char** argv)
{
    /*----COMMAND SEND----*/
    {
        if (argc > 1)
        {
            if ((argc == 3) && (!strcmp("-c", argv[1])) && (strlen(argv[2]) < 12))
            {

                g_sock_cmd = new UnixSocket;
                int buff_size = 11;

                g_sock_cmd->clientStart(FILE_SOCK_CMD, buff_size);
                if (!g_sock_cmd->clientConnect())
                {
                    strcpy(g_sock_cmd->getBuff(), "0-");
                    strcpy((g_sock_cmd->getBuff() + 2), argv[2]);
                    g_sock_cmd->sendLine(strlen(argv[2]) + 2);


                    g_sock_cmd->clientGap();
                    return 0;
                }
                else
                {
                    printf("Error socket conn\n");
                    return -1;
                }

            }
            else if ((argc == 2) && (!strcmp("-d", argv[1])))
            {
                g_deamon_run = 1;
            }
            else if ((argc == 2) && (!strcmp("-h", argv[1])))
            {
                printf("%s", HELP);
                exit(EXIT_SUCCESS);
            }
            else
            {
                printf("Use -h to see help\n");
                exit(EXIT_SUCCESS);
            }
        }
    }

    if (daemon_status(FILE_PID)) return -1;


    /*----PARSE CONFIG----*/
    vCjson json;
    char* file_content = nullptr;

    {
        long long int file_size = -1;
        int fd;

        //get file len
        struct stat finfo;
        if (!stat(FILE_CONF, &finfo)) file_size = (long long int)finfo.st_size;

        //read file
        if (file_size > 0)
        {
            fd = open (FILE_CONF, O_RDONLY);
            if (fd > 0)
            {
                file_content = new char[file_size + 1];
                if ((read(fd, file_content, file_size)) == file_size)
                {
                    file_content[file_size] = 0x00;
                    close(fd);
                }
                else
                {
                    printf("\nError: read server.conf\nExit.\n");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                printf("\nError: open server.conf\nExit.\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            printf("\nError: server.conf is miss\nExit.\n");
            exit(EXIT_FAILURE);
        }
        //printf("%s\n", file_content);

        if (json.parse(file_content) != VCJSON_OK)
        {
            printf("\nError: server.conf parse\nExit.\n");
            exit(EXIT_FAILURE);
        }
        //showTree(&json, 0);
    }



    ceres_init();
    sig_handler_conf();

    char res;



    /*----SERIAL PORT----*/
    int*            l;
    unsigned char*  f;
    st_uart_conf uart_conf;

    {
        unsigned char success = 1;

        //get uart config data from file
        if (!json.go_node_name("uart"))
        {
            //json.show_node();
            if (!json.go_node_child())
            {
                if(!json.go_node_name_on_layer("path"))
                {
                    if (json.get_node_type() == VCJSON_NT_PNV)
                    {
                        uart_conf.path = new char[json.get_node_value_len() + 1];
                        memcpy(uart_conf.path, json.get_node_value_ptr(), json.get_node_value_len());
                        uart_conf.path[json.get_node_value_len()] = 0x00;
                        printf("uart path: [%s]\n", uart_conf.path);

                        if(!json.go_node_name_on_layer("speed"))
                        {
                            if (json.get_node_type() == VCJSON_NT_NUM)
                            {
                                uart_conf.speed = strtol(json.get_node_value_str(), nullptr, 10);
                                printf("uart speed: [%i]\n", uart_conf.speed);

                                if(!json.go_node_name_on_layer("wait response"))
                                {
                                    if (json.get_node_type() == VCJSON_NT_NUM)
                                    {
                                        uart_conf.wait_response= strtol(json.get_node_value_str(), nullptr, 10);
                                        printf("uart wait_response: [%i]\n", uart_conf.wait_response);

                                        if(!json.go_node_name_on_layer("delay tx"))
                                        {
                                            if (json.get_node_type() == VCJSON_NT_NUM)
                                            {
                                                uart_conf.delay_tx= strtol(json.get_node_value_str(), nullptr, 10);
                                                printf("uart delay_tx: [%i]\n", uart_conf.delay_tx);

                                                if(!json.go_node_name_on_layer("delay rx"))
                                                {
                                                    if (json.get_node_type() == VCJSON_NT_NUM)
                                                    {
                                                        uart_conf.delay_rx= strtol(json.get_node_value_str(), nullptr, 10);
                                                        printf("uart delay_rx: [%i]\n", uart_conf.delay_rx);
                                                    }
                                                    else success = 0;
                                                }
                                                else success = 0;
                                            }
                                            else success = 0;
                                        }
                                        else success = 0;
                                    }
                                    else success = 0;
                                }
                                else success = 0;
                            }
                            else success = 0;
                        }
                        else success = 0;
                    }
                    else success = 0;
                }
                else success = 0;
            }
            else success = 0;
        }
        else success = 0;

        if (!success)
        {
            printf("\nError: server.conf uart settings\nExit.\n");
            exit(EXIT_FAILURE);
        }

        //init uart
        g_uart = new SerialPort;

        if (g_uart->init(1, uart_conf.path, uart_conf.speed, UART_BUFF_SIZE, uart_conf.delay_tx, uart_conf.delay_rx, uart_conf.wait_response))
        {
            printf("Error!\nCant init uart. Code:%i\n", g_uart->state);
            exit(EXIT_FAILURE);
        };

        if (g_uart->begin())
        {
            printf("Error!\nCant run uart. Code:%i\n", g_uart->state);
            exit(EXIT_FAILURE);
        }

        l = &(g_uart->len);
        f = g_uart->buff;
    }


    /*----PREPARE FILES & DIRS----*/
    {
        int fd;


        umask(0);

        if (mkdir(RUN_DIR, 0731) && (errno != EEXIST))
        {
            free();
            return -1;
        }

        if (mkdir(ETC_DIR, 0751) && (errno != EEXIST))
        {
            free();
            return -1;
        }

        //SHMEM token file
        fd = open(FILE_SHMEM_TOK, O_CREAT | O_RDONLY, S_IRUSR | S_IRGRP | S_IROTH);
        if (fd < 0)
        {
            free();
            return -1;
        }
        close(fd);

        //SHNEM size file
        fd = open(FILE_SHMEM_SIZE, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if (fd < 0)
        {
            free();
            return -1;
        }
        close(fd);
    }


    /*----GET DEVS INFO FROM DB----*/
    {
        g_num_devices = 2;
        g_arr_devices = new device[g_num_devices];

        //from DB
        unsigned char db_dev_data[g_num_devices][2] = {{2, 9},{1, 38}};

        for (int i = 0; i < g_num_devices; i++)
            init_device(g_arr_devices + i, db_dev_data[i][0], db_dev_data[i][1]);


#ifdef DEBUG
        for (int i = 0; i < g_num_devices; i++)
            printf("ao: %02x, as: %02x, gk: %02x, type: %i, z: %i, r: %i\n",
                   g_arr_devices[i].addr_o,
                   g_arr_devices[i].addr_s,
                   g_arr_devices[i].g_key,
                   g_arr_devices[i].type,
                   g_arr_devices[i].num_zones,
                   g_arr_devices[i].num_relays
                  );
#endif // DEBUG

        /*KDL*/
        g_arr_devices[0].qnt_zones = 7;
        g_arr_devices[0].zones = new st_zone[g_arr_devices[0].qnt_zones];
        for (int i = 0; i < g_arr_devices[0].qnt_zones; i++)
            g_arr_devices[0].zones[i].part = 0;

        g_arr_devices[0].zones[0].num = 2;
        g_arr_devices[0].zones[1].num = 3;
        g_arr_devices[0].zones[2].num = 6;
        g_arr_devices[0].zones[3].num = 7;
        g_arr_devices[0].zones[4].num = 8;
        g_arr_devices[0].zones[5].num = 78;
        g_arr_devices[0].zones[6].num = 79;



        /*UPS*/
        g_arr_devices[1].qnt_zones = 5;
        g_arr_devices[1].zones = new st_zone[g_arr_devices[1].qnt_zones];
        for (int i = 0; i < g_arr_devices[1].qnt_zones; i++)
            g_arr_devices[1].zones[i].part = 0;

        g_arr_devices[1].zones[0].num = 1;
        g_arr_devices[1].zones[1].num = 2;
        g_arr_devices[1].zones[2].num = 3;
        g_arr_devices[1].zones[3].num = 4;
        g_arr_devices[1].zones[4].num = 5;




        /*ADC*/

        /*intinteger*/
        g_num_zones_adc_int = 3;
        g_arr_zones_adc_int = new st_entity[g_num_zones_adc_int];

        //humidity
        g_arr_zones_adc_int[0].p_device = &g_arr_devices[0];
        g_arr_zones_adc_int[0].p_zone = &g_arr_devices[0].zones[4];

        //AKB %
        g_arr_zones_adc_int[1].p_device = &g_arr_devices[1];
        g_arr_zones_adc_int[1].p_zone = &g_arr_devices[1].zones[3];

        //U in
        g_arr_zones_adc_int[2].p_device = &g_arr_devices[1];
        g_arr_zones_adc_int[2].p_zone = &g_arr_devices[1].zones[4];

        /*double*/
        g_num_zones_adc_dbl = 4;
        g_arr_zones_adc_dbl = new st_entity[g_num_zones_adc_dbl];

        g_arr_zones_adc_dbl[0].p_device = &g_arr_devices[0];
        g_arr_zones_adc_dbl[0].p_zone = &g_arr_devices[0].zones[3];

        //U out
        g_arr_zones_adc_dbl[1].p_device = &g_arr_devices[1];
        g_arr_zones_adc_dbl[1].p_zone = &g_arr_devices[1].zones[0];

        //I out
        g_arr_zones_adc_dbl[2].p_device = &g_arr_devices[1];
        g_arr_zones_adc_dbl[2].p_zone = &g_arr_devices[1].zones[1];

        //U akkb
        g_arr_zones_adc_dbl[3].p_device = &g_arr_devices[1];
        g_arr_zones_adc_dbl[3].p_zone = &g_arr_devices[1].zones[2];



        /*PARTS*/
        g_num_parts = 1;
        g_arr_parts = new st_part[g_num_parts];

        g_arr_parts[0].num = 1;
        g_arr_parts[0].zones_num = 2;

        g_arr_parts[0].entities = new st_entity[g_arr_parts[0].zones_num];

        //1st zone
        g_arr_parts[0].entities[0].p_device = &g_arr_devices[0];
        g_arr_parts[0].entities[0].p_zone = &g_arr_devices[0].zones[0];
        g_arr_devices[0].zones[0].part = 1;
        g_arr_devices[0].zones[0].p_part = &(g_arr_parts[0]);

        //2nd zone
        g_arr_parts[0].entities[1].p_device = &g_arr_devices[0];
        g_arr_parts[0].entities[1].p_zone = &g_arr_devices[0].zones[2];
        g_arr_devices[0].zones[2].part = 1;
        g_arr_devices[0].zones[2].p_part = &(g_arr_parts[0]);

    }


    /*----INIT SHMEM----*/
    {
        key_t key;
        int fd;
        int memsize;
        char buff[100];

        fd = open(FILE_SHMEM_SIZE, O_WRONLY);

        /*DEVS_INFO*/
        {
            memsize = g_num_devices * SHMEM_SIZE_DEV;

            if ((key = ftok(FILE_SHMEM_TOK, SHMEM_ID_DEVS_INFO)) < 0) //cant get token
            {
                printf("Can\'t generate key\n");
                free();
                return -1;
            }

            if ((g_shmid_devs = shmget(key, memsize, 0666|IPC_CREAT|IPC_EXCL)) < 0) // try to make shmem
            {
                if(errno != EEXIST) // no shmem, but error, or various size of mem
                {
                    printf("Can\'t create shared memory\n");
                    free();
                    return -1;
                }
                //has old shmem
                if((g_shmid_devs = shmget(key, memsize, 0)) < 0) // try to get descriptor
                {
                    printf("Can\'t find shared memory\n");
                    free();
                    return -1;
                }
            }

            if ((g_shmem_devs = (unsigned char*)shmat(g_shmid_devs, NULL, 0)) == (unsigned char*)(-1))
            {
                printf("Can't attach shared memory\n");
                free();
                return -1;
            }

            memset(g_shmem_devs, 0x00, memsize);

            snprintf(buff, 99, "%i-%i\n", SHMEM_ID_DEVS_INFO, memsize);
            write(fd, buff, strlen(buff));

            int offset;
            for (int i = 0; i < g_num_devices; i++)
            {
                offset = SHMEM_SIZE_DEV * i;

                g_arr_devices[i].states_self_ptr = g_shmem_devs + 2 + offset;

                //addr, type, state1, state2, state3, num_zones, num_relays
                g_shmem_devs[offset + 0] = g_arr_devices[i].addr_o;
                g_shmem_devs[offset + 1] = g_arr_devices[i].type;
                g_shmem_devs[offset + 2] = 0x00;
                g_shmem_devs[offset + 3] = 0x00;
                g_shmem_devs[offset + 4] = 0x00;
                g_shmem_devs[offset + 5] = g_arr_devices[i].num_zones;
                g_shmem_devs[offset + 6] = g_arr_devices[i].num_relays;
            }


        }

        /*ST_ZONES*/
        {
            memsize = 0;

            for (int i = 0; i < g_num_devices; i++)
                memsize += g_arr_devices[i].num_zones + g_arr_devices[i].num_relays;

            if ((key = ftok(FILE_SHMEM_TOK, SHMEM_ID_ST_ZONES)) < 0) //cant get token
            {
                printf("Can\'t generate key\n");
                free();
                return -1;
            }

            if ((g_shmid_states = shmget(key, memsize, 0666|IPC_CREAT|IPC_EXCL)) < 0) // try to make shmem
            {
                if(errno != EEXIST) // no shmem, but error, or various size of mem
                {
                    printf("Can\'t create shared memory\n");
                    free();
                    return -1;
                }
                //has old shmem
                if((g_shmid_states = shmget(key, memsize, 0)) < 0) // try to get descriptor
                {
                    printf("Can\'t find shared memory\n");
                    free();
                    return -1;
                }
            }

            if ((g_shmem_states = (unsigned char*)shmat(g_shmid_states, NULL, 0)) == (unsigned char*)(-1))
            {
                printf("Can't attach shared memory\n");
                free();
                return -1;
            }

            memset(g_shmem_states, 0x00, memsize);

            snprintf(buff, 99, "%i-%i\n", SHMEM_ID_ST_ZONES, memsize);
            write(fd, buff, strlen(buff));

            //get begin states arr pointer
            g_arr_devices[0].states_zones_ptr = g_shmem_states;
            for (int i = 1; i < g_num_devices; i++)
                g_arr_devices[i].states_zones_ptr = g_arr_devices[i - 1].states_zones_ptr + g_arr_devices[i - 1].num_zones + g_arr_devices[i - 1].num_relays;
        }

        /*ST_PARTS*/
        {
            memsize = SHMEM_MAX_PARTS;

            if ((key = ftok(FILE_SHMEM_TOK, SHMEM_ID_ST_PARTS)) < 0) //cant get token
            {
                printf("Can\'t generate key\n");
                free();
                return -1;
            }

            if ((g_shmid_parts = shmget(key, memsize, 0666|IPC_CREAT|IPC_EXCL)) < 0)  // try to make shmem
            {
                if(errno != EEXIST) // no shmem, but error, or various size of mem
                {
                    printf("Can\'t create shared memory\n");
                    free();
                    return -1;
                }
                //has old shmem
                if((g_shmid_parts = shmget(key, memsize, 0)) < 0) // try to get descriptor
                {
                    printf("Can\'t find shared memory\n");
                    free();
                    return -1;
                }
            }

            if ((g_shmem_parts = (unsigned char*)shmat(g_shmid_parts, NULL, 0)) == (unsigned char*)(-1))
            {
                printf("Can't attach shared memory\n");
                free();
                return -1;
            }

            memset(g_shmem_parts, 0x00, memsize);

            snprintf(buff, 99, "%i-%i\n", SHMEM_ID_ST_PARTS, memsize);
            write(fd, buff, strlen(buff));
        }

        /*ADC LONG INT*/
        {
            memsize = g_num_zones_adc_int * sizeof(long int);

            if ((key = ftok(FILE_SHMEM_TOK, SHMEM_ID_ADC_LI)) < 0) //cant get token
            {
                printf("Can\'t generate key\n");
                free();
                return -1;
            }

            if ((g_shmid_adc_li = shmget(key, memsize, 0666|IPC_CREAT|IPC_EXCL)) < 0)  // try to make shmem
            {
                if(errno != EEXIST) // no shmem, but error, or various size of mem
                {
                    printf("Can\'t create shared memory\n");
                    free();
                    return -1;
                }
                //has old shmem
                if((g_shmid_adc_li = shmget(key, memsize, 0)) < 0) // try to get descriptor
                {
                    printf("Can\'t find shared memory\n");
                    free();
                    return -1;
                }
            }

            if ((g_shmem_adc_li = (long int*)shmat(g_shmid_adc_li, NULL, 0)) == (long int*)(-1))
            {
                printf("Can't attach shared memory\n");
                free();
                return -1;
            }

            memset(g_shmem_adc_li, 0x00, memsize);

            snprintf(buff, 99, "%i-%i\n", SHMEM_ID_ADC_LI, memsize);
            write(fd, buff, strlen(buff));
        }

        /*ADC DOUBLE*/
        {
            memsize = g_num_zones_adc_dbl * sizeof(double);

            if ((key = ftok(FILE_SHMEM_TOK, SHMEM_ID_ADC_DBL)) < 0) //cant get token
            {
                printf("Can\'t generate key\n");
                free();
                return -1;
            }

            if ((g_shmid_adc_dbl = shmget(key, memsize, 0666|IPC_CREAT|IPC_EXCL)) < 0)  // try to make shmem
            {
                if(errno != EEXIST) // no shmem, but error, or various size of mem
                {
                    printf("Can\'t create shared memory\n");
                    free();
                    return -1;
                }
                //has old shmem
                if((g_shmid_adc_dbl = shmget(key, memsize, 0)) < 0) // try to get descriptor
                {
                    printf("Can\'t find shared memory\n");
                    free();
                    return -1;
                }
            }

            if ((g_shmem_adc_dbl = (double*)shmat(g_shmid_adc_dbl, NULL, 0)) == (double*)(-1))
            {
                printf("Can't attach shared memory\n");
                free();
                return -1;
            }

            memset(g_shmem_adc_dbl, 0x00, memsize);

            snprintf(buff, 99, "%i-%i\n", SHMEM_ID_ADC_DBL, memsize);
            write(fd, buff, strlen(buff));
        }

        close(fd);

#ifdef DEBUG
        printf("\nDEVS INFO\n");
        int offset = 0;
        for (int i = 0; i < g_num_devices; i++)
        {
            for (int j = 0; j < SHMEM_SIZE_DEV; j++) printf("%i ", *(g_shmem_devs + offset + j));

            printf("\n");

            offset += SHMEM_SIZE_DEV;
        }

#endif

    }


    /*----INIT SOCKETS----*/
    {
        /*command socket*/
        {
            g_sock_cmd = new UnixSocket;
            if (g_sock_cmd->serverStart(FILE_SOCK_CMD, SOCK_CMD_QUE, SOCK_CMD_BUFF_SIZE))
            {
                free();
                return -1;
            }
        }
    }

    if (g_deamon_run)
    {
        if (daemon_start(FILE_PID, &g_pid_fd))
        {
            free();
            exit(EXIT_FAILURE);
        }
    }

    /*----POLLING----*/

    st_event event;
    st_zone* p_cmd_zone = nullptr;
    st_part* p_cmd_part = nullptr;


    /*initial poll*/
    {


        for (int d = 0; d < g_num_devices; d++)
        {
            /*sec mode*/
            {
                ceres_q_sec_begin(f, l, &g_arr_devices[d].addr_o, &g_arr_devices[d].g_key);
                uart_poll();
            }

            /*devs state*/
            {
                ceres_q_state_simp(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key, 0);
                uart_poll();

                ceres_r_state_simp(f, l, &g_arr_devices[d].addr_s, 0, &g_arr_devices[d].states_obt, event.states_buff);

                //local store
                memcpy(g_arr_devices[d].states_local, event.states_buff, 5);

                //shmem store
                memcpy(g_arr_devices[d].states_self_ptr, event.states_buff, 3);
            }

            /*zones state*/
            {
                for (int z = 0; z < g_arr_devices[d].qnt_zones; z++)
                {
                    ceres_q_state_simp(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key, g_arr_devices[d].zones[z].num);
                    uart_poll();

                    ceres_r_state_simp(f, l, &g_arr_devices[d].addr_s, g_arr_devices[d].zones[z].num, &g_arr_devices[d].zones[z].states_obt, event.states_buff);
                    //local store
                    memcpy(g_arr_devices[d].zones[z].states_local, event.states_buff, 5);

                    //shmem store
                    *(g_arr_devices[d].states_zones_ptr + g_arr_devices[d].zones[z].num - 1) = event.states_buff[0];
                }
            }

            /*parts states*/
            {
                unsigned char   armed;
                unsigned char   prohibitory_event_priority;
                st_entity*         prohibitory_entity;

                for (int p = 0; p < g_num_parts; p++)
                {
                    armed = 1;
                    prohibitory_event_priority = 0;

                    for (int z = 0; z < g_arr_parts[p].zones_num; z++)
                    {
                        if (g_arr_parts[p].entities[z].p_zone->states_local[0] != 0x18)
                        {
                            if (g_arr_parts[p].entities[z].p_zone->states_local[0] > prohibitory_event_priority)
                            {
                                prohibitory_event_priority = g_arr_parts[p].entities[z].p_zone->states_local[0]; //for search max value
                                prohibitory_entity = &g_arr_parts[p].entities[z]; //for next using
                            }
                            armed = 0;
                        }
                    }

                    if (!armed) //not armed
                    {
                        if (prohibitory_entity->p_zone->states_local[0] == 0x6D) // having only disarmed prohibitory zones
                        {
                            //store local
                            g_arr_parts[p].state_local = 0xF2;

                            //store shmem
                            g_shmem_parts[g_arr_parts[p].num - 1] = 0xF2;
                        }
                        else
                        {
                            //store local
                            g_arr_parts[p].state_local = prohibitory_entity->p_zone->states_local[0];

                            //store shmem
                            g_shmem_parts[g_arr_parts[p].num - 1] = prohibitory_entity->p_zone->states_local[0];
                        }
                    }
                    else //armed
                    {
                        //store local
                        g_arr_parts[p].state_local = 0xF1;

                        //store shmem
                        g_shmem_parts[g_arr_parts[p].num - 1] = 0xF1;
                    }

                }
            }

        }


        /*ADC read*/
        {
            for (int i = 0; i < g_num_zones_adc_int; i++)
            {

                usleep(200000);

                ceres_q_adc_v2(f, l, &g_arr_zones_adc_int[i].p_device->addr_s, &g_arr_zones_adc_int[i].p_device->g_key, g_arr_zones_adc_int[i].p_zone->num);
                if (!uart_poll())
                {
                    unsigned char* str = ceres_r_adc_v2(f, l, &g_arr_zones_adc_int[i].p_device->addr_s);
                    ceres_extract_adc(str, &g_shmem_adc_li[i]);
                }
            }

            for (int i = 0; i < g_num_zones_adc_dbl; i++)
            {

                usleep(200000);

                ceres_q_adc_v2(f, l, &g_arr_zones_adc_dbl[i].p_device->addr_s, &g_arr_zones_adc_dbl[i].p_device->g_key, g_arr_zones_adc_dbl[i].p_zone->num);
                if (!uart_poll())
                {
                    unsigned  char* str = ceres_r_adc_v2(f, l, &g_arr_zones_adc_dbl[i].p_device->addr_s);
                    ceres_extract_adc(str, &g_shmem_adc_dbl[i]);
                }
            }
        }

    }


//    int flag = 0;
    while (1)
    {
        if (g_term) break;

        usleep(DELAY);



        if (!unblockRead(g_sock_cmd->getSrvDscr(), 0, 5))
        {
#ifdef DEBUG
            printf("\nHas connection\n");
#endif // DEBUG
            g_sock_cmd->serverHandleConnection();
            g_sock_cmd->readLine();
#ifdef DEBUG
            printf("Client says: %s\n", g_sock_cmd->getBuff());
#endif // DEBUG

            res = cmd_parse(g_sock_cmd->getBuff());
            if (!res)
            {
                device* dev = nullptr;

                if (cmd.addr > 0) //action w/ zone or relay
                {
                    for (int i = 0; i < g_num_devices; i++)
                    {
                        if (g_arr_devices[i].addr_o == cmd.addr)
                        {
                            dev = &g_arr_devices[i];
                            break;
                        }
                    }

                    if (dev)
                    {
                        for (int i = 0; i < dev->qnt_zones; i++)
                        {
                            if (dev->zones[i].num == cmd.num)
                            {
                                p_cmd_zone = &dev->zones[i];
                                p_cmd_zone->is_polled = 0;
                                break;
                            }
                        }

                        if (cmd.type == CMD_TYPE_Z_ARM)
                        {
                            ceres_q_zone_arm(f, l, &dev->addr_s, &dev->g_key, cmd.num);

#ifdef CMD_SHOW
                            printf("<--- CMD:arm_zone addr:%i zone:%i\n", dev->addr_o, cmd.num);
#endif // CMD_SHOW

                            if (!uart_poll())
                            {

                                if (ceres_r_zone_arm(f, l, &dev->addr_s, cmd.num))
                                {
                                    /*err proc*/
                                    p_cmd_zone = nullptr;
                                }
                            }
                        }
                        else if (cmd.type == CMD_TYPE_Z_DISARM)
                        {
                            ceres_q_zone_disarm(f, l, &dev->addr_s, &dev->g_key, cmd.num);

#ifdef CMD_SHOW
                            printf("<--- CMD:disarm_zone addr:%i zone:%i\n", dev->addr_o, cmd.num);
#endif // CMD_SHOW

                            if (!uart_poll())
                            {

                                if (ceres_r_zone_disarm(f, l, &dev->addr_s, cmd.num))
                                {
                                    /*err proc*/
                                    p_cmd_zone = nullptr;
                                }
                            }
                        }
                        else if (cmd.type == CMD_TYPE_R_ON)
                        {
                            ceres_q_relay_on(f, l, &dev->addr_s, &dev->g_key, cmd.num);

#ifdef CMD_SHOW
                            printf("<--- CMD:on_relay addr:%i relay:%i\n", dev->addr_o, cmd.num);
#endif // CMD_SHOW

                            if (!uart_poll())
                            {

                                if (ceres_r_relay_on(f, l, &dev->addr_s, cmd.num))
                                {
                                    /*err proc*/
                                }
                            }
                        }
                        else if (cmd.type == CMD_TYPE_R_OFF)
                        {
                            ceres_q_relay_off(f, l, &dev->addr_s, &dev->g_key, cmd.num);

#ifdef CMD_SHOW
                            printf("<--- CMD:off_relay addr:%i relay:%i\n", dev->addr_o, cmd.num);
#endif // CMD_SHOW

                            if (!uart_poll())
                            {

                                if (ceres_r_relay_off(f, l, &dev->addr_s, cmd.num))
                                {
                                    /*err proc*/
                                }
                            }
                        }
                        else
                        {
                            /*err proc*/
                            p_cmd_zone = nullptr;
                        }
                    }
                }
                else  //action w/ part
                {

                    for (int i = 0; i < g_num_parts; i++)
                    {
                        if (cmd.num == g_arr_parts[i].num)
                        {
                            p_cmd_part = &g_arr_parts[i];
                            break;
                        }
                    }

                    if (p_cmd_part)
                    {
                        p_cmd_part->zones_polled = 0; //drop polled zones counter

                        if (cmd.type == CMD_TYPE_P_ARM)
                        {
                            /*
                            generate event
                            */
#ifdef CMD_SHOW
                            printf("<--- CMD:arm_part num:%i\n", cmd.num);
#endif // CMD_SHOW

                            for (int z = 0; z < p_cmd_part->zones_num; z++) // for each zone of the part
                            {

                                ceres_q_zone_arm(f, l, &p_cmd_part->entities[z].p_device->addr_s, &p_cmd_part->entities[z].p_device->g_key, p_cmd_part->entities[z].p_zone->num);
                                p_cmd_part->entities[z].p_zone->is_polled = 0;
#ifdef CMD_SHOW
                                printf("<--- CMD:arm_zone addr:%i zone:%i\n", p_cmd_part->entities[z].p_device->addr_o, p_cmd_part->entities[z].p_zone->num);
#endif // CMD_SHOW
                                usleep(DELAY);

                                if (!uart_poll())
                                {
                                    if (ceres_r_zone_arm(f, l, &p_cmd_part->entities[z].p_device->addr_s, p_cmd_part->entities[z].p_zone->num))
                                    {
                                        /*err proc*/
                                    }
                                }

                            }

                        }
                        else if (cmd.type == CMD_TYPE_P_DISARM)
                        {
                            /*
                            generate event
                            */
#ifdef CMD_SHOW
                            printf("<--- CMD:disarm_part num:%i\n", cmd.num);
#endif // CMD_SHOW


                            for (int z = 0; z < p_cmd_part->zones_num; z++) // for each zone of the part
                            {

                                ceres_q_zone_disarm(f, l, &p_cmd_part->entities[z].p_device->addr_s, &p_cmd_part->entities[z].p_device->g_key, p_cmd_part->entities[z].p_zone->num);

                                p_cmd_part->entities[z].p_zone->is_polled = 0;

#ifdef CMD_SHOW
                                printf("<--- CMD:disarm_zone addr:%i zone:%i\n", p_cmd_part->entities[z].p_device->addr_o, p_cmd_part->entities[z].p_zone->num);
#endif // CMD_SHOW

                                usleep(DELAY);

                                if (!uart_poll())
                                {
                                    if (ceres_r_zone_arm(f, l, &p_cmd_part->entities[z].p_device->addr_s, p_cmd_part->entities[z].p_zone->num))
                                    {
                                        /*err proc*/
                                    }
                                }

                            }

                        }
                        else
                        {
                            p_cmd_part = nullptr;
                        }
                    }
                }
            }
            else
            {
                //send error reply to cmd_socket

                printf("\nCMD ERROR\n");
            }



//            *gSockCmd->getBuff() = char(res);
//            g_sock_cmd->sendLine(1);
            g_sock_cmd->clientGap();

        }


        for (int d = 0; d < g_num_devices; d++)
        {
            /*read event*/
            {
                ceres_q_read_event(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key);

                uart_poll();

                switch (g_arr_devices[d].type)
                {
                case 9:
                    res = ceres_09_event_type(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key, &event.type, &event.num);
                    break;

                default:
                    res = 1;
                    event.type = event.num = 0; //??

                    /*send load new event*/
                    {
                        ceres_q_load_event(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key);
                        g_uart->send();
                        usleep (200000);
                    }
                }
            }

            /*proc event*/
            {
                //no event
                if (res == 1) continue; // for (int d = 0; d < g_num_devices; d++)

                //has event
                else if (res == 0)
                {
                    event.zone_num = 0xFF;

                    /*proc event*/
                    {
                        if (g_arr_devices[d].type == 9) // KDL
                        {
                            if (event.type == CERES_ET_ACCESS)
                            {

                            }
                            else if (event.type == CERES_ET_RELAY)
                            {
                                ceres_09_event_relay(f, &event.num, &event.relay_num, &event.relay_prog);
                                store_event_relay(&event.num, &g_arr_devices[d].addr_o,  &event.relay_num, &event.relay_prog);

                            }
                            else
                            {
                                ceres_09_event_common(f, &event.num, &event.zone_num);
                                store_event_zone(&event.num, &g_arr_devices[d].addr_o,  &event.zone_num);
                            }
                        }
                    }

                    /*send load new event*/
                    {
                        ceres_q_load_event(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key);
                        g_uart->send();
                        usleep (200000);
                    }

                    /*update zone state*/
                    {
                        if (event.zone_num < 0xFF)
                        {
                            ceres_q_state_simp(f, l, &g_arr_devices[d].addr_s, &g_arr_devices[d].g_key, event.zone_num);
                            uart_poll();

                            ceres_r_state_simp(f, l, &g_arr_devices[d].addr_s, event.zone_num, &event.states_obtained, event.states_buff);


                            st_zone* p_zone = nullptr;
                            st_part* p_part = nullptr;

                            for (int z = 0; z < g_arr_devices[d].qnt_zones; z++)
                            {
                                if (g_arr_devices[d].zones[z].num == event.zone_num)
                                {
                                    p_zone = &g_arr_devices[d].zones[z];
                                    p_part = (st_part*)(p_zone->p_part);
                                    break;
                                }
                            }

                            if (event.zone_num > 0) //  zone state
                            {
                                //local store
                                memcpy(p_zone->states_local, event.states_buff, 5);

                                //shmem store
                                *(g_arr_devices[d].states_zones_ptr + event.zone_num - 1) = event.states_buff[0];

                                //check part
                                if ((p_part) && (p_part->state_local == 0xF1))
                                {
                                    p_part->state_local = g_shmem_parts[p_part->num - 1] = event.states_buff[0];

                                    /*
                                    generate event
                                    */
                                }


                                /*p_cmd_flags flag*/
                                {
                                    if (p_cmd_part)
                                    {
                                        for (int i = 0; i < p_cmd_part->zones_num; i++)
                                        {
                                            if (p_cmd_part->entities[i].p_zone == p_zone)
                                            {
                                                if(!p_cmd_part->entities[i].p_zone->is_polled)
                                                {
                                                    p_cmd_part->entities[i].p_zone->is_polled = 1;
                                                    p_cmd_part->zones_polled++;
                                                }
                                            }
                                        }

                                        if (p_cmd_part->zones_polled == p_cmd_part->zones_num)
                                        {

#ifdef CMD_SHOW
                                            printf("<--> CMD:COMPLETE part:%i\n", p_cmd_part->num);
#endif // CMD_SHOW
                                            unsigned char success = 1;

                                            if (cmd.type == CMD_TYPE_P_ARM)
                                            {
                                                for (int i = 0; i < p_cmd_part->zones_num; i++)
                                                {
                                                    if (p_cmd_part->entities[i].p_zone->states_local[0] != 0x18)
                                                    {
                                                        success = 0;
                                                        break;
                                                    }
                                                }

                                                if (success)
                                                {
                                                    //local store
                                                    p_cmd_part->state_local = 0xF1;

                                                    //shmem store
                                                    g_shmem_parts[p_cmd_part->num - 1] = 0xF1;
                                                }
                                                else
                                                {
                                                    //local store
                                                    p_cmd_part->state_local = 0x11;

                                                    //shmem store
                                                    g_shmem_parts[p_cmd_part->num - 1] = 0x11;
                                                }
                                            }
                                            else if (cmd.type == CMD_TYPE_P_DISARM)
                                            {
                                                //local store
                                                p_cmd_part->state_local = 0xF2;

                                                //shmem store
                                                g_shmem_parts[p_cmd_part->num - 1] = 0xF2;
                                            }

                                            /*
                                            send to cliend
                                            .
                                            .
                                            */
                                            p_cmd_part = nullptr;
                                        }
                                    }
                                    else if (p_cmd_zone)
                                    {

                                        if (p_cmd_zone == p_zone)
                                        {
#ifdef CMD_SHOW
                                            printf("<--> CMD:COMPLETE addr:%i zone:%i\n", g_arr_devices[d].addr_o, p_cmd_zone->num);
#endif // CMD_SHOW
                                            /*
                                            send to cliend
                                            .
                                            .
                                            */

                                            p_cmd_zone = nullptr;

                                        }


                                    }
                                }
                            }
                            else // dev state
                            {
                                //local store
                                memcpy(g_arr_devices[d].states_local, event.states_buff, 5);

                                //shmem store
                                memcpy(g_arr_devices[d].states_self_ptr, event.states_buff, 3);
                            }
                        }
                    }
                }

                //error
                else
                {
                    //err proc
                }
            }


        }

    }

    //sleep(10);
    free();
}

void free()
{
    shmctl(g_shmid_devs, IPC_RMID, nullptr);
    shmctl(g_shmid_states, IPC_RMID, nullptr);
    shmctl(g_shmid_parts, IPC_RMID, nullptr);
    shmctl(g_shmid_adc_li, IPC_RMID, nullptr);
    shmctl(g_shmid_adc_dbl, IPC_RMID, nullptr);

    unlink(FILE_SOCK_CMD);
    unlink(FILE_SHMEM_TOK);
    unlink(FILE_SHMEM_SIZE);
    unlink(FILE_PID);
    rmdir(RUN_DIR);


    if (g_arr_zones_adc_int) delete []g_arr_zones_adc_int;
    if (g_arr_zones_adc_dbl) delete []g_arr_zones_adc_dbl;

    if (g_arr_parts) delete []g_arr_parts;

    if (g_arr_devices != nullptr)
    {
        for (int i = 0; i < g_num_devices; i++)
        {
            if (g_arr_devices[i].zones)
                delete []g_arr_devices[i].zones;
        }

        delete []g_arr_devices;
        g_arr_devices = nullptr;
        g_num_devices = 0;

    }
};

int uart_poll()
{
    if(!g_uart->send())
        if (!g_uart->recv());
    return 0;

    return g_uart->state;
};

int unblockRead (int fd, int sec, int usec)
{
    fd_set _fs;
    struct timeval _timeout;
    _timeout.tv_sec  = sec;
    _timeout.tv_usec = usec;
    FD_ZERO (&_fs); // обнуляем набор дескрипторов файлов
    FD_SET(fd, &_fs); //  добавляем в набор дескриптор открытого файла ком-порта

    switch (select ( fd+1, &_fs, NULL, NULL, &_timeout ))
    {
    case 0:
        return 1; //empty set
        break;

    case -1:
        return -1; //error
        break;

    default:
        if (FD_ISSET(fd, &_fs)) return 0; //set has fd
        else return 2;  // set hasn't fd
    }
};

void store_event_zone (unsigned char* event, unsigned char* addr, unsigned char* zone_num)
{
    printf("%s code:%i addr:%i zone:%i\n", CERES_EVENT_DESC_ARR[*event], *event, *addr, *zone_num);
    return;
};

void store_event_relay (unsigned char* event, unsigned char* addr, unsigned char* relay_num, unsigned char* prog)
{
    printf("%s code:%i addr:%i relay:%i prog:%i\n", CERES_EVENT_DESC_ARR[*event], *event, *addr, *relay_num, *prog);
    return;
};

/*DEMON*/
int daemon_status(const char* pid_file_path)
{
    chdir("/"); // переходим в корень

    struct stat finfo;
    if (stat(pid_file_path, &finfo) == 0 ) // file exists
    {
        int fd = open(pid_file_path, O_RDONLY);
        if (fd > 0)
        {
            char buff[17] = {};
            if (read(fd, buff, 17) >= 0)
            {
                printf("\nERROR.\nPid file [%s] has been detected.\n", pid_file_path);

                unsigned long lockPID = strtoul(buff, NULL, 10);

#ifdef DEBUG
                printf ("PID from %s: %ld\n", pid_file_path, lockPID);
#endif
                /*Проверяем запущен ли процесс с lockPID. В функ. kill(), мы посылаем процессу с lockPID 0-сигнал, с помощью которого можно проверить наличие процесса*/

                if (kill(lockPID, 0) == 0) // процесс есть
                {
                    printf("It's owned by the active process with PID [%ld].\nPlease check it.\n\n", lockPID);
                }
                else
                {
                    if (errno == ESRCH) // процесса нет
                    {
                        printf("It's owned by the process with PID [%ld], which is now defunct.\nPlease delete the lock file and try again.\n\n", lockPID);
                    }
                    else // иная ошибка
                    {
                        perror("ERROR.\nInvalid signal or not have permissions ");
                    }
                }

            }
            else
                perror("ERROR.\nCan't read pid file ");

            close(fd);
        }
        else
            perror("ERROR.\nCan't open pid file ");

        close(fd);
    }
    else return 0;

    return -1;
};
int daemon_start(const char* pid_file_path, int* pid_file_fd)
{
    chdir("/"); // переходим в корень
    *pid_file_fd = open (pid_file_path, O_RDWR | O_CREAT | O_EXCL, 0644); // пытаемся создать pid файл
    if (*pid_file_fd == -1)
    {
        perror("ERROR.\nCan't create lock file ");
        return -1; //Can't create lock file
    }
    /**Структура для установки блокировки*/
    flock exLock;
    /*Устанавливаем блокировку на pid файл*/
    exLock.l_type = F_WRLCK;
    exLock.l_whence = SEEK_SET;
    exLock.l_start = (off_t)0;
    exLock.l_len = (off_t)0;
    if (fcntl(*pid_file_fd, F_SETLK, &exLock) < 0) //блокировка не удалась
    {
        close(*pid_file_fd);
        perror("ERROR.\nCan't apply exclusive lock, can't get lock file ");
        return -1; //Can't apply exclusive lock, can't get lock file
    }
    pid_t currPID = fork();
    switch (currPID)
    {
    case 0: // потомок
        break;

    case -1: //
        perror("ERROR.\nInitial fork faild ");
        return -1; //Initial fork faild
        break;

    default: // родитель, закрываем
        _exit(0);
        break;
    }

    if (setsid() < 0) // начинаем новую сессию, становимся ее лидером и единственным участником
        return -1; // Can't init session

    /*Сохраняем PID в файл*/
    if (ftruncate(*pid_file_fd, 0) < 0) // обрезаем pid файл до 0 для сохранения PID процесса
        return -1; //Truncate loc file faild
    char buff[17];
    memset (buff, 0x00, 17); // очищаем буфер хранения pid
    sprintf(buff, "%d\n", (int)getpid()); // записываем текущий PID в буфер
    write(*pid_file_fd, buff, strlen(buff)); // сохраняем в файл

    /*Закрываем все дескрипторы кроме нужных нам*/
    for (int i = 0; i <= 2; i++)
    {
        close (i);
    }
    /*Открываем дескрипторы потоков stdin (0) stdout (1) stderr (2) и отправляем их в /dev/null*/
    umask(0);
    int stdioer = open ("/dev/null", O_RDWR); // fd 0 = stdin
    dup(stdioer); // fd 1 = stdout
    dup(stdioer); // fd 2 = stderr


    setpgrp();
    return 0;

};

/*SIGNALS*/
int sig_handler_conf()
{
    /*Структуры для назначения сигналам функций обработчиков*/
    struct sigaction sig_TERM; // сигнал завершения (^c)

    struct sigaction sig_CONT; // // works with systemctl. WHY???

    /*Игнорируемые сигналы*/
    signal(SIGUSR1,     SIG_IGN);
    signal(SIGUSR2,     SIG_IGN);
    signal(SIGPIPE,     SIG_IGN); //сигнал обрыва клиента сокета
    signal(SIGALRM,     SIG_IGN);
    //signal(SIGTERM, SIG_IGN);
    signal(SIGTSTP,     SIG_IGN);
    signal(SIGTTIN,     SIG_IGN);
    signal(SIGTTOU,     SIG_IGN);
    signal(SIGURG,      SIG_IGN);
    signal(SIGXCPU,     SIG_IGN);
    signal(SIGXFSZ,     SIG_IGN);
    signal(SIGVTALRM,   SIG_IGN);
    signal(SIGPROF,     SIG_IGN);
    signal(SIGIO,       SIG_IGN);
    signal(SIGCHLD,     SIG_IGN);
    signal(SIGHUP,      SIG_IGN);



    /*Фатальные сигналы, при получении которых работа демона не желательна*/
    signal(SIGQUIT,     sig_FATAL_handler);
    signal(SIGILL,      sig_FATAL_handler);
    signal(SIGTRAP,     sig_FATAL_handler);
    signal(SIGABRT,     sig_FATAL_handler);
    signal(SIGIOT,      sig_FATAL_handler);
    signal(SIGBUS,      sig_FATAL_handler);
    signal(SIGFPE,      sig_FATAL_handler);
    signal(SIGSEGV,     sig_FATAL_handler);
    signal(SIGSTKFLT,   sig_FATAL_handler);
    signal(SIGCONT,     sig_FATAL_handler);
    signal(SIGPWR,      sig_FATAL_handler);
    signal(SIGSYS,      sig_FATAL_handler);

    /*Обработчики*/

    /*Сигнал  TERM*/
    sig_TERM.sa_handler = sig_TERM_handler;
    sigemptyset(&sig_TERM.sa_mask);
    sig_TERM.sa_flags = 0;
    sigaction(SIGTERM, &sig_TERM, NULL);

    /*Сигнал  CONT*/
    // works with systemctl. WHY???
    sig_CONT.sa_handler = sig_CONT_handler;
    sigemptyset(&sig_CONT.sa_mask);
    sig_CONT.sa_flags = 0;
    sigaction(SIGCONT, &sig_CONT, NULL);

    return 0;
}
void sig_FATAL_handler(int sig)
{
    free();
    exit(EXIT_FAILURE);
    return;
}
void sig_TERM_handler(int sig)
{
    g_term = 1; // works with systemctl. WHY???
    return;
};
void sig_CONT_handler(int sig) // works with systemctl. WHY???
{
    return;
};

