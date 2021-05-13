#include <iostream>
#include <iomanip>
#include <string>
using namespace std;

const char* pack = "$GPGLL,4717.11634,N,00833.91297,E,124923.00,A,A*6E\r\n";
const char* pack1 = "$GPGLL,,,,,124924.00,V,N*42\r\n";
const char* pack2 = "$GPGLL,,,,,,V,N*64\r\n";

#include <stdio.h>
#include <string.h>
#include <cstdlib>

int nmea0183_checksum(const char *nmea_data)
{
    int crc = 0;
    int i;

    // the first $ sign and the last two bytes of original CRC + the * sign
    for (i = 1; i < strlen(nmea_data) - 5; i ++) {
        crc ^= nmea_data[i];
    }

    return crc;
}

bool nmea(const char *d)
{
    int l = strlen(d);
    char crc[3] = {d[l-4],d[l-3],0};
    return nmea0183_checksum(d) == strtol(crc,nullptr,16);
}

int main()
{
    cout<<hex<<nmea0183_checksum(pack2)<<" - "<<nmea(pack2);

    return 0;
}