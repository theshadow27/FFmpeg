#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define SIZE 5000*1000
uint8_t buf[SIZE];

main() {
    int hist[188] = {0};
    int i=0, j, len, pid, afc, is_start;
    while(!feof(stdin)) {
        buf[i++] = getchar();
    }
    len = i-1;

    for (i=0; i<len; i+=188) {
        is_start = buf[i+1] & 0x40;
        pid = ((buf[i+1]<<8) + buf[i+2]) & 0x1FFF;
        afc = (buf[i+3] >> 4) & 3;

#if 1
        for(j=0; j<188; j++)
            fprintf(stderr, "%02X ", buf[i+j]);
        fprintf(stderr, "\n");
#endif

        if (buf[i] != 0x47) {
            fprintf(stderr, "Fixing 0x%02X->0x47\n", buf[i]);
            buf[i] = 0x47;
        }
        // 1008, 4072 1001 1011
        if (pid != 0 && pid != 8191 && pid != 1000 && pid != 32 && pid != 64) {
            fprintf(stderr, "PID %d -> 1000\n", pid);
            pid = 1000;
        }
        if (pid == 1000 && !(afc&1)) {
            fprintf(stderr, "AFC %d -> %d\n", afc, afc|1);
            afc |= 1;
        }
        if (is_start && pid == 1000) {
            int j;
            for (j=0; j<188-3; j++) {
                if (buf[i+j] == 0 && buf[i+j+1] == 0 && buf[i+j+2] == 1)
                    break;
            }
            if (j == 188-3) {
                fprintf(stderr, "discarding is start\n");
                is_start = 0;
            }
        }
//without 0x47 fixing 3372
//without pid=1000 3356
//without afc|=1 3624
//ref 3728
//without is_start=0 2532

        buf[i+1] = (buf[i+1] & ~0x5F) + (pid>>8) + is_start;
        buf[i+2] = pid;
        buf[i+3] = (buf[i+3] & ~0x30) + (afc << 4);
    }
    for (i=0; i<len; i++)
        putchar(buf[i]);
}