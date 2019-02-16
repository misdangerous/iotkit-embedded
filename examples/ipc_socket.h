#ifndef __IPC_SOCKET_H
#define __IPC_SOCKET_H

#define FCONVE_NAME "freq_converter"
#define FEED_LORA_NAME "feed_lora"
#define TEMP_NAME "temp_uart"
#define NORMAL_NAME "normal_watch"


#define FEED_HUM      0xE103
#define FREQ_FREQSET  0x9001
#define FREQ_FREQRUN  0x9000
#define FREQ_OUTPOWER 0x9006
#define FREQ_STATUS   0xB000
#define FREQ_ERROR    0xB001

#define TEMP_VALUE    0x1100
#define TEMP_OUTPOWER 0x1200
#define TEMP_ERROR    0x3000

#define VOLTAGE_A 0x0000
#define VOLTAGE_B 0x0001
#define VOLTAGE_C 0x0002
#define CURRENT_A 0x0003
#define CURRENT_B 0x0004
#define CURRENT_C 0x0005

int Request_Commond(int fd, char * name, int addr, int reg_start, int reg_number, unsigned short *DataBuf);

#endif