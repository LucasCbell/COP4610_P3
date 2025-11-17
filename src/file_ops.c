#include "common.h"


void read_boot_sector(FILE* img, unsigned char* buf) {

    // buf will be 512 characters long (FAT32) and stores the boot sector
    for(int i = 0; i < 512; i++) {
        buf[i] = fgetc(img);
    }
}

BPB parse_boot_sector(BPB & b, unsigned char* boot_sector) {

    // these are the important parts


    // these are the rest just for funsies
    

}