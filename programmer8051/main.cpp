#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <cstdlib>
#include <cstdint>

#include "Serial.h"

//#define DEBUG

class Memory8051
{
private:
    uint32_t sector_flags[16];
    char memory[65536];

    void writeByte(int address, char byte)
    {
        setFlag(address);
        memory[address] = byte;
    }

    void setFlag(int address)
    {
        address = (address & 0xff80)>>7;
        int a = address & 0x1f;
        int b = address>>5;
        sector_flags[b] |= 1<<a;
    }

    bool getFlag(int n_Sector)
    {
        int a = n_Sector & 0x1f;
        int b = n_Sector>>5;

        if(sector_flags[b] & (1<<a))
            return true;

        return false;
    }

public:
    Memory8051()
    {
        for (int a=0; a<16; a++)
            sector_flags[a] = 0;
    }

    int writeSector(int address, char* array, int length)
    {
        if(length < 1)
            return 0;

        int a;

        for ( a=0; a<length; a++, address++)
        {
            writeByte(address, array[a]);
        }

        return a;
    }

    char* getSector(int n_Sector)
    {
        if(getFlag(n_Sector))
            return memory+(n_Sector*128);
        else
            return NULL;
    }
};

void delay(int);
bool startCommunication(CSerial* port);
void runMcu(CSerial*);
int programMcu(CSerial* , Memory8051* );
int checkFileSyntax(FILE*);
int readHexFile(FILE* , Memory8051*);
unsigned int hexStringToInt(char* , int );
void writeMcuSector(CSerial* , int , char* , int );
int readMcuSector(CSerial* , int , char* , int );
bool eraseMcuSector(CSerial* , int , int );
bool compareArrays(char* , char* , int );
void sendSerialData(CSerial* , char* , int );
int readSerialData(CSerial* , char* , int , int );
int error(int );
int getArguments(int argc, char *argv[], int*, char*);

/*****************************************************************************************/
/*****************************************************************************************/
int main(int argc, char *argv[])
{
    //char* port_name = "/dev/ttyUSB0";
    char port_name[] = "COM4";
    char file_name[256];
    int port_num;
    int baud_rate = 9600;

    std::cout << "YAEasyIAP" << std::endl;
    std::cout << "Yet Another EasyIAP for SST89E516DR2" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << std::endl;

    if(getArguments(argc, argv, &port_num, file_name) != 0)
        return 1;

    FILE* FileRead;

    FileRead = fopen(argv[2], "r");

    if (FileRead == NULL)
        return error(1);

    if( error(checkFileSyntax(FileRead)))
        return 0;

    Memory8051 Memory;
    readHexFile(FileRead , &Memory);

    fclose(FileRead);

    CSerial com_port;

    if (!com_port.Open(port_num, baud_rate))
        return error(4);

    std::cout << "COM" << port_num << " opened" << std::endl;
    std::cout << "PRESS [Reset Button] to start or <Ctrl-C> to abort" << std::endl;

    for (int a=0; a<10; a++)
    {
        std::cout << "Connecting" << std::endl;

        if(startCommunication(&com_port))
        {
            std::cout << "Device connected" << std::endl;
	    delay(2000);

            if(error(programMcu(&com_port, &Memory)) == 0)
                std::cout << "Programming done" << std::endl;

            runMcu(&com_port);
            std::cout << "Device running" << std::endl;

            com_port.Close();

            return 0;              // successful exit 
        }
    }

    com_port.Close();

    return error(5);               // failed exit

}
/*****************************************************************************************/
/*****************************************************************************************/

int programMcu(CSerial* port, Memory8051* Memory)
{
    int n_sectors = 0;
    int sectors[512];

    for(int a = 0; a < 512; a++)
    {
        char* array = Memory->getSector(a);
        if(array != NULL)
        {
            sectors[n_sectors] = a;
            n_sectors++;
        }
    }

    for(int a = 0; a < n_sectors; a++)
    {
        if(!eraseMcuSector(port, sectors[a]*128, 1))
            return 8;

        delay(5);

#ifndef DEBUG
        std::cout << "\rErasing " << ((a+1)*100)/n_sectors << "%" << std::flush;
#endif
    }

    std::cout << std::endl;

    for(int a = 0; a < n_sectors; a++)
    {
        char* array = Memory->getSector(sectors[a]);
        char sector[128];

        writeMcuSector(port, sectors[a]*128, array, 128);

        if(readMcuSector(port, a*128, sector, 128) != 128) 
            return 6;

        if(!compareArrays(sector, array, 128))
            return 6;

#ifdef DEBUG
        std::cout << "Sector " << a << " Matched" << std::endl;
#endif

#ifndef DEBUG
        std::cout << "\rProgramming " << ((a+1)*100)/n_sectors << "%" << std::flush;
#endif
    }

    std::cout << std::endl;

    return 0;
}

void runMcu(CSerial* port)
{
    char run_cmd[] = {(char)0x62, (char)0x62};

    sendSerialData(port, run_cmd, 2);
}

bool eraseMcuSector(CSerial* port, int address, int n_sector)
{
    char run_cmd[4] = {(char)0x0b};

    run_cmd[1] = (address>>8) & 0xff;
    run_cmd[2] = address & 0xff;
    run_cmd[3] = n_sector & 0xff;

/*
    printf( "%02X %02X %02X %02X\n",
           (unsigned char)run_cmd[0], (unsigned char)run_cmd[1],
           (unsigned char)run_cmd[2], (unsigned char)run_cmd[3] );
*/

    sendSerialData(port, run_cmd, 4);
    delay(20);

    if (readSerialData(port, run_cmd, 1, 5) != 1) 
        return false;

    if(run_cmd[0] != (char)0xc0) 
        return false;
	
    return true;
}

void writeMcuSector(CSerial* port, int address, char* array, int n_bytes)
{
    char run_cmd[4] = {(char)0x0e};
    run_cmd[1] = (address>>8) & 0xff;
    run_cmd[2] = address & 0xff;
    run_cmd[3] = n_bytes & 0xff;

/*
    printf( "%02X %02X %02X %02X ",
           (unsigned char)run_cmd[0], (unsigned char)run_cmd[1],
           (unsigned char)run_cmd[2], (unsigned char)run_cmd[3] );

    printf( "%02X %02X %02X %02X\n",
           (unsigned char)array[0], (unsigned char)array[1],
           (unsigned char)array[2], (unsigned char)array[3] );
*/

    sendSerialData(port, run_cmd, 4);
    sendSerialData(port, array, n_bytes);
}

int readMcuSector(CSerial* port, int address, char* array, int n_bytes)
{
    char run_cmd[4] = {(char)0x0c};
    run_cmd[1] = (address>>8) & 0xff;
    run_cmd[2] = address & 0xff;
    run_cmd[3] = n_bytes & 0xff;

/*
    printf( "%02X %02X %02X %02X\n",
           (unsigned char)run_cmd[0], (unsigned char)run_cmd[1],
           (unsigned char)run_cmd[2], (unsigned char)run_cmd[3] );
*/

    sendSerialData(port, run_cmd, 4);
    delay(220);

    return readSerialData(port, array, n_bytes, 10);
}

bool startCommunication(CSerial* port)
{
    char baud_test[] = {(char)0x80, (char)0xe0, (char)0xf8, (char)0xfe,
                        (char)0x80, (char)0xe0, (char)0xf8, (char)0xfe,
                        (char)0x80, (char)0xe0, (char)0xf8, (char)0xfe,
                        (char)0x80, (char)0xe0, (char)0xf8, (char)0xfe};

    char start_link[] = {'B','S','L',(char)0x05, (char)0x55, (char)0x60};
    char check[4];

    delay(5);

    for (int a=0; a<20; a++)
    {
        sendSerialData(port, baud_test, 16);
        delay(20);

        if (readSerialData(port, check, 2, 3) == 2)
	    break;

        delay(100);
    }

    if(!compareArrays(check, (char*)"OK", 2))
        return false;

#ifdef DEBUG
    std::cout << "OK" << std::endl;
#endif

    sendSerialData(port, start_link, 3);
    delay(5);

    if (readSerialData(port, check, 3, 3) != 3)
        return false;

    if(!compareArrays(check, (char*)"RDY", 3))
        return false;

#ifdef DEBUG
    std::cout << "RDY" << std::endl;
#endif

    sendSerialData(port, start_link+3, 2);
    delay(5);

    if (readSerialData(port, check, 1, 3) != 1)
        return false;

/*
    if(check[0] != 0x08)                           // this may differ, skip check
        return false;
*/

    sendSerialData(port, start_link+5, 1);
    delay(5);

    if (readSerialData(port, check, 2, 3) != 2)
        return false;

/*
    if((check[0] != 0x11) || (check[1] != 0x46))   // this may differ, skip check
        return false;
*/

    return true;
}


int checkFileSyntax(FILE* file_to_check)
{
    char line[256];

    if (file_to_check == NULL)
        return 1;

    rewind (file_to_check);

    while(fgets (line , 255 , file_to_check) != NULL)
    {
        int len = strlen(line) -1;

        if(len<11)
            return 2;

        if(line[0] != ':')
            return 2;

        int n_bytes = (len-1)/2;
        int bytes[n_bytes];
        int check_sum = 0;

        for (int a=0; a<n_bytes; a++)
        {
            bytes[a] = hexStringToInt(line+1+(a*2), 2);
            check_sum += bytes[a];
        }

        if(bytes[0] != (n_bytes - 5))
            return 2;

        check_sum -= bytes[n_bytes-1];

        if(bytes[n_bytes-1] != ((-check_sum) & 0xff))
            return 3;
    }

    return 0;
}

int readHexFile(FILE* file_to_check, Memory8051* Memory)
{
    char line[256];

    if (file_to_check == NULL)
        return 0;

    rewind (file_to_check);

    int n = 0;

    while(fgets (line , 255 , file_to_check) != NULL)
    {
        int n_bytes = (strlen(line) -2)/2;
        unsigned char bytes[n_bytes];

        for (int a=0; a<n_bytes; a++)
        {
            bytes[a] = hexStringToInt(line+1+(a*2), 2);
        }

        int len = bytes[0];
        int addr = bytes[2]+(bytes[1]*256);

        if(bytes[3] == 0)
            n += Memory->writeSector(addr, (char*)(bytes+4), len);
    }

    return n;
}

void delay(int millis)
{
    usleep(millis*1000);
}

unsigned int hexStringToInt(char* hex_string, int len)
{
    unsigned int hex_int;
    std::stringstream ss;
    char string_in[len+1];

    memcpy(string_in, hex_string, len);
    string_in[len] = 0;

    ss << std::hex << string_in;
    ss >> hex_int;

    return hex_int;
}

void sendSerialData(CSerial* port, char* array, int len)
{
    port->SendData(array, len);
}

int readSerialData(CSerial* port, char* array, int len, int n_attempts)
{
    int n_bytes = 0;                      // number of bytes read

    while (n_attempts--) {
        n_bytes += port->ReadData(array + n_bytes, len - n_bytes);

#ifdef DEBUG
        std::cout << n_bytes << " ";
#endif

        if (n_bytes == len) 
            break;                        // return exactly the required number of characters

        if (n_bytes > len)                // this is very unlikely 
            break;                        // return more characters read than required

	delay(5);
    }

#ifdef DEBUG
    std::cout << std::endl;
#endif

    return n_bytes;                       // return actual number of characters read
}

bool compareArrays(char* array1, char* array2, int len)
{
    for(int a=0; a<len; a++)
    {
        if(array1[a] != array2[a])
            return false;
    }

    return true;
}

int getArguments(int argc, char *argv[], int* port_num, char* file_name)
{
    if(argc != 3)
    {
        std::cout << "Usage: yaeasyiap.exe <port_num> <filename.hex>" << std::endl;
        std::cout << "Example: yaeasyiap.exe 1 ";
	std::cout << "\"800294 - Mini Controller v20220909@0823 (SST).hex\"" << std::endl;

        return 1;
    }

    *port_num = atoi(argv[1]);
    strcpy(file_name, argv[2]);

    return 0;
}

int error(int code)
{
    switch (code)
    {
        case 1:
            std::cout << "file no found" << std::endl;
            return 1;
        case 2:
        case 3:
            std::cout << "file corrupted" << std::endl;
            return 1;
        case 4:
            std::cout << "can't open port" << std::endl;
            return 1;
        case 5:
            std::cout << "device is not responding" << std::endl;
            return 1;
        case 6:
            std::cout << "error during verify" << std::endl;
            return 1;
        case 7:
            std::cout << "too few arguments" << std::endl;
            return 1;
        case 8:
            std::cout << "error erasing" << std::endl;
            return 1;
        default:
            break;
    }

    return 0;
}
