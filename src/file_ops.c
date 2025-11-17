#include "common.h"


void read_boot_sector(FILE* img, unsigned char* buf) {

    // buf will be 512 characters long (FAT32) and stores the boot sector
    for(int i = 0; i < 512; i++) {
        buf[i] = fgetc(img);
    }
}