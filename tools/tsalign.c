#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define SIZE 5000*1000
uint8_t buf[SIZE];

#define RB32(p) ((unsigned)((p)[0]<<24) + ((p)[1]<<16) + ((p)[2]<<8) + (p)[3])
#define RL32(p) ((unsigned)((p)[3]<<24) + ((p)[2]<<16) + ((p)[1]<<8) + (p)[0])

#define RBIT(p, i, n) ((RB32(p+((i)>>3)) << ((i)&7))>>(32-(n)))
// #define RBIT(p, i, n) ((RL32(p+((i)>>3)) >> ((i)&7)) & ((-1U)>>(32-(n))))

static void flush(uint8_t *p, int start, int end)
{
    int i;
    int written = 0;

    for (i=start; i<end; i+=8) {
        putchar(RBIT(p, i, 8));
        written++;
    }
    while (written % 188) {
        putchar(0xFF);
        written ++;
    }
}

int main(int argc, char **argv) {
    int i=0, len;
    int last_match = -1;
    int next_out = 0;
    while(!feof(stdin)) {
        buf[i++] = getchar();
    }
    len = i-1;

    for (i=0; i<len*8; i++) {
        if (RBIT(buf, i, 8) == 0x47) {
            int score =  (RBIT(buf, i +   8*188, 8) == 0x47)
                        +(RBIT(buf, i + 2*8*188, 8) == 0x47)
                        +(RBIT(buf, i + 3*8*188, 8) == 0x47)
                        +(RBIT(buf, i + 4*8*188, 8) == 0x47)
                        +(RBIT(buf, i + 5*8*188, 8) == 0x47)
                        +(RBIT(buf, i + 6*8*188, 8) == 0x47)
//                         +(RBIT(buf, i + 7*8*188, 8) == 0x47)
                        /*+(RBIT(buf, i + 8*8*188, 8) == 0x47)*/;
            if(score > 2) {
                if (i % (8*188) == last_match)
                    continue;
                fprintf(stderr, "New match at %d %d score %d\n", i>>3, i&7, score);
                last_match = i % (8*188);
                flush(buf, next_out, i);
                next_out = i;
            }
        }
    }
    flush(buf, next_out, i);

    return 0;
}