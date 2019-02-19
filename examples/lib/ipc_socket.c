#include "ipc_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "iot_export.h"
#include <unistd.h>

#define EXAMPLE_TRACE(...)                               \
    do {                                                     \
        HAL_Printf("\033[1;32;40m%s.%d: ", __func__, __LINE__);  \
        HAL_Printf(__VA_ARGS__);                                 \
        HAL_Printf("\033[0m\r\n");                                   \
    } while (0)


int RevData_Analyze(char * Str_buffer, unsigned short *RevData)
{
    char *token;
    int data_tmp;
    int i = 0;
    token = strtok(Str_buffer,",");
	while(token != NULL){
		sscanf(token, "%d", &data_tmp);
		RevData[i++] = (unsigned short)data_tmp;
        //EXAMPLE_TRACE("RevData:%d index:%d", (int)RevData[i-1], i-1);
		token = strtok(NULL,",");
	}
    return 0;
}

int Request_Commond(int fd, char * name, int addr, int reg_start, int reg_number, unsigned short *DataBuf)
{
    char commond[512];
    char buffer[1024];
    char rec_name[20];
    int rev_addr = 0,rev_reg_start,rev_reg_num = 0,data_tmp;
    int rc;

    if(name == NULL){
        EXAMPLE_TRACE("name is null\n");
        return -1;
    }

    snprintf(commond, 100, "%s:4,%d,%04X,%d\n", name, addr, reg_start, reg_number);
    EXAMPLE_TRACE("send:%s",commond);
    write(fd, commond, strlen(commond));

    int nread_len;
    nread_len = read(fd, buffer, 1024);
    buffer[nread_len] = 0;

    if(nread_len > 0){
        EXAMPLE_TRACE(" recv:%s",buffer);
        rc = sscanf(buffer, "%[^:]:%[^\n]\n", rec_name, commond);
        
        if(rc <= 1){
            EXAMPLE_TRACE("sense name no find");
            return -1;
        }

        if(strstr(name,rec_name) == NULL){
            EXAMPLE_TRACE("sense name is error");
            return -1;
        }

	    sscanf(commond, "%d,%d,%X,%d,%s", \
		    &data_tmp, &rev_addr, &rev_reg_start, &rev_reg_num, buffer);

        if(data_tmp == 4){
            //EXAMPLE_TRACE("slave_addr:%d reg_start:%d reg_num:%d", rev_addr, rev_reg_start, rev_reg_num);
            if((rev_addr == addr) && (reg_start == rev_reg_start) && (rev_reg_num == reg_number)){
                /* 解析得出数据 */
                RevData_Analyze(buffer, DataBuf);
                return 0;
            }
            else{
                EXAMPLE_TRACE("commond data is error");
                return -1;
            }
            
        }
        else{
            EXAMPLE_TRACE("commond is error");
            return -1;
        }
    }
    return -1;
}


int Send_Commond(int fd, char * name, int addr, int reg_start, unsigned short DataBuf)
{
    char commond[512];
    char buffer[1024];

    if(name == NULL){
        EXAMPLE_TRACE("name is null\n");
        return -1;
    }

    snprintf(commond, 100, "%s:3,%d,%04X,1,%d\n", name, addr, reg_start, DataBuf);
    EXAMPLE_TRACE("send:%s",commond);
    write(fd, commond, strlen(commond));

    int nread_len;
    nread_len = read(fd, buffer, 1024);
    buffer[nread_len] = 0;

    if(nread_len > 0){
        EXAMPLE_TRACE("recv:%s",buffer);
        if( strstr(buffer, "error") == NULL ){
            if(strcmp(commond, buffer) == 0){
                return 0;
            }
        }
    }
    return -1;
}
