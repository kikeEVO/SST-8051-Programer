#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <cstdlib>

#include <QCoreApplication>
#include <QSerialPort>
#include <QByteArray>
#include <QTimer>

class Memory8051
{
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
private:
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
        else
            return false;
    }
    uint32_t sector_flags[16];
    char memory[65536];
};

bool configSerialPort(QSerialPort*, char*);
void delay(int);
void resetMcu(QSerialPort*);
bool startComunication(QSerialPort*);
void runMcu(QSerialPort*);
int programMcu(QSerialPort* , Memory8051* );
int checkFileSyntax(FILE*);
int readHexFile(FILE* , Memory8051*);
unsigned int hexStringToInt(char* , int );
void writeMcuSector(QSerialPort* , int , char* , int );
int readMcuSector(QSerialPort* , int , char* , int );
bool eraseMcuSector(QSerialPort* , int , int );
bool compareArrays(char* , char* , int );
void sendSerialData(QSerialPort* , char* , int );
int readSerialData(QSerialPort* , char* , int , int );
bool configSerialPort(QSerialPort* , char* , int );
int error(int );
int getArguments(int argc, char *argv[], char**, char**, int*);

/*****************************************************************************************/
/*****************************************************************************************/
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    //char* port_name = "/dev/ttyUSB0";
    char* port_name = NULL;
    char* file_name = NULL;
    int baud_rate = 9600;
    if(getArguments(argc, argv, &file_name, &port_name, &baud_rate) != 0)
        return 1;

    FILE* FileRead;
    FileRead = fopen(file_name, "r");
    if (FileRead == NULL)
        return error(1);

    if( error(checkFileSyntax(FileRead)))
        return 0;

    Memory8051 Memory;
    readHexFile(FileRead , &Memory);

    fclose(FileRead);

    QSerialPort puerto;
    if(!configSerialPort(&puerto, port_name, baud_rate))
        return error(4);

    for (int a=0; a<5; a++)
    {
        if(startComunication(&puerto))
        {
            std::cout << "Device connected" << std::endl;
            if(error(programMcu(&puerto, &Memory)) == 0)
                std::cout << "Programing done" << std::endl;
            runMcu(&puerto);
            std::cout << "Device runnig" << std::endl;
            break;
        }
        else
        {
            error(5);
        }
    }
    //68 68
    puerto.close();

    return 0;

}
/*****************************************************************************************/
/*****************************************************************************************/

int programMcu(QSerialPort* port, Memory8051* Memory)
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
        std::cout << "\rErasing " << ((a+1)*100)/n_sectors << "%" << std::flush;
    }
    std::cout << std::endl;
    for(int a = 0; a < n_sectors; a++)
    {
        char* array = Memory->getSector(sectors[a]);
        writeMcuSector(port, sectors[a]*128, array, 128);
        delay(100);
        std::cout << "\rPrograming " << ((a+1)*100)/n_sectors << "%" << std::flush;
    }
    std::cout << std::endl;
    for(int a = 0; a < n_sectors; a++)
    {
        char* array = Memory->getSector(sectors[a]);
        char sector[128];
        if(readMcuSector(port, a*128, sector, 128) < 128)
            if(!compareArrays(sector, array, 128))
                return 6;
        std::cout << "\rVerifing " << ((a+1)*100)/n_sectors << "%" << std::flush;
    }
    std::cout << std::endl;
    return 0;
}
void runMcu(QSerialPort* port)
{
    char run_cmd[] = {(char)0x62, (char)0x62};
    sendSerialData(port, run_cmd, 2);
}
bool eraseMcuSector(QSerialPort* port, int address, int n_sector)
{
    char run_cmd[4] = {(char)0x0b};
    run_cmd[1] = (address>>8) & 0xff;
    run_cmd[2] = address & 0xff;
    run_cmd[3] = n_sector & 0xff;

    sendSerialData(port, run_cmd, 4);

    readSerialData(port, run_cmd, 1, 1000);
    if(run_cmd[0] != (char)0xc0)
        return false;

    return true;
}
void writeMcuSector(QSerialPort* port, int address, char* array, int n_bytes)
{
    char run_cmd[4] = {(char)0x0e};
    run_cmd[1] = (address>>8) & 0xff;
    run_cmd[2] = address & 0xff;
    run_cmd[3] = n_bytes & 0xff;

    sendSerialData(port, run_cmd, 4);
    sendSerialData(port, array, n_bytes);
}
int readMcuSector(QSerialPort* port, int address, char* array, int n_bytes)
{
    char run_cmd[4] = {(char)0x0c};
    run_cmd[1] = (address>>8) & 0xff;
    run_cmd[2] = address & 0xff;
    run_cmd[3] = n_bytes & 0xff;

    sendSerialData(port, run_cmd, 4);

    return readSerialData(port, array, n_bytes, 1000);
}
void resetMcu(QSerialPort* port)
{
    port->setRequestToSend(false);
    delay(1);
    port->setRequestToSend(true);
    // 160ms to start code
}

bool startComunication(QSerialPort* port)
{
    char baud_test[] = {(char)0x80, (char)0xe0, (char)0xf8, (char)0xfe,
                        (char)0x80, (char)0xe0, (char)0xf8, (char)0xfe,
                        (char)0x80, (char)0xe0, (char)0xf8, (char)0xfe,
                        (char)0x80, (char)0xe0, (char)0xf8, (char)0xfe};

    char start_link[] = {'B','S','L',(char)0x05, (char)0x55};
    char check[4];

    resetMcu(port);
    delay(5);

    sendSerialData(port, baud_test, 16);
    readSerialData(port, check, 2, 1000);
    if(!compareArrays(check, (char*)"OK", 2))
        return false;

    delay(5);

    sendSerialData(port, start_link, 3);
    readSerialData(port, check, 3, 1000);
    if(!compareArrays(check, (char*)"RDY", 2))
        return false;

    delay(5);
    sendSerialData(port, start_link+3, 2);
    readSerialData(port, check, 1, 1000);
    //if((check[0] != 0x0) && (check[0] != 0x40))
    //    return false;

    return true;
}
bool configSerialPort(QSerialPort* port, char* name, int baud_rate)
{
    if(baud_rate > 38400)
        baud_rate = 38400;
    else if(baud_rate < 1200)
        baud_rate = 1200;
    else
    {
        baud_rate = baud_rate/1200;
        baud_rate = baud_rate*1200;
    }

    port->setPortName(name);
    //port->setBaudRate(QSerialPort::Baud38400);
    port->setBaudRate(baud_rate);
    port->setDataBits(QSerialPort::Data8);
    port->setParity(QSerialPort::NoParity);
    port->setStopBits(QSerialPort::OneStop);
    port->setFlowControl(QSerialPort::NoFlowControl);
    port->setReadBufferSize(256);

    if(port->open(QIODevice::ReadWrite))
        return true;
    else
        return false;
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
    QTimer ti;
    ti.setInterval(millis);
    ti.setSingleShot(true);
    ti.start();
    while(ti.remainingTime() > 0);
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
void sendSerialData(QSerialPort* port, char* array, int len)
{
    port->write(array, len);
    port->flush();
    while(port->bytesToWrite() > 0);
}
int readSerialData(QSerialPort* port, char* array, int len, int msec)
{
    QTimer time_out;
    time_out.setSingleShot(true);
    time_out.start(msec);
    do
    {
        port->waitForReadyRead(50);
        if (port->bytesAvailable() >= len)
            break;
    }while(time_out.remainingTime() > 0);

    QByteArray ba = port->readAll();
    memcpy(array, ba.data(), ba.size());

    return ba.size();
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

int getArguments(int argc, char *argv[], char** file_name, char** port_name, int* baud_rate)
{
    if(argc < 2)
    {
        std::cout << "Usage: prog8051 [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  -P <port>" << std::endl;
        std::cout << "  -F <filename.hex>" << std::endl;
        std::cout << "  -b <baudrate>" << std::endl;
        return 1;
    }

    for(int a=1; a<argc; a++)
    {
        QByteArray argunment = argv[a];
        if(argunment.startsWith("-P"))
        {
            if(argunment.size() > 2)
                *port_name = argv[a]+2;
            else
                *port_name = argv[++a];
        }
        else if (argunment.startsWith("-F"))
        {
            if(argunment.size() > 2)
                *file_name = argv[a]+2;
            else
                *file_name = argv[++a];
        }
        else if (argunment.startsWith("-b"))
        {
            if(argunment.size() > 2)
                *baud_rate = atoi(argv[a]+2);
            else
                *baud_rate = atoi(argv[++a]);
        }
        else
            return 1;
    }
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
            std::cout << "can't open Port" << std::endl;
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
