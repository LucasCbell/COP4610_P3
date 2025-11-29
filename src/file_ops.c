#include "common.h"


void read_boot_sector(FILE* img, unsigned char* buf) {

    // buf will be 512 characters long (FAT32) and stores the boot sector
    for(int i = 0; i < 512; i++) {
        buf[i] = fgetc(img);
    }
}

void parse_boot_sector(BPB *b, unsigned char* boot_sector) {

    // these are the important parts
    // BytesPerSec;     // short
    // SecPerClus;      // char
    // RsvdSecCnt;      // short
    // NumFATs;         // char
    // TotSec32;        // int
    // FATSz32;         // int
    // RootClus;        // int

    // all bits are stored in little endian, so to convert
    //  to big endian shift bits to correct position
    b->BytesPerSec = boot_sector[11] | (boot_sector[12] << 8);
    //printf("BytesPerSec is: %d\n", b->BytesPerSec);
    // we move boot_sector[12] by 8 bits because itself has 4 bits
    // and boot_sector[11] has 4 bits. it ends up infront
    
    b->SecPerClus = boot_sector[13];   // single byte
    //printf("SecPerClus is: %d\n", b->SecPerClus);

    b->RsvdSecCnt = boot_sector[14] | (boot_sector[15] << 8);
    //printf("RsvdSecCnt is: %d\n", b->RsvdSecCnt);

    b->NumFATs = boot_sector[16];
    //printf("NumFATs: %d\n", b->NumFATs);

    b->TotSec32 = boot_sector[32] | (boot_sector[33] << 8) |
                   (boot_sector[34] << 16) | (boot_sector[35] << 24);
    //printf("TotalSec32: %d\n", b->TotSec32);

    b->FATSz32 = boot_sector[36] | (boot_sector[37] << 8) |
                   (boot_sector[38] << 16) | (boot_sector[39] << 24);
    //printf("FATSz32: %d\n", b->FATSz32);

    b->RootClus = boot_sector[44] | (boot_sector[45] << 8) |
                   (boot_sector[46] << 16) | (boot_sector[47] << 24);
    //printf("RootClus: %d\n", b->RootClus);

    // these can be assigned later if needed
    memset(b->jmpBoot, 0, sizeof(b->jmpBoot));  // sets all to 0
    memset(b->OEMName, 0, sizeof(b->OEMName));
    b->RootEntCnt = 0;     
    b->TotSec16 = 0;        
    b->Media = 0;            
    b->FATSz16 = 0;         
    b->SecPerTrk = 0;       
    b->NumHeads = 0;        
    b->ExtFlags = 0;        
    b->FSVer = 0;           
    b->BkBootSec = 0;       
    memset(b->Reserved, 0, sizeof(b->Reserved));
    b->DrvNum = 0;           
    b->Reserved1 = 0;        
    b->BootSig = 0;          
    b->VolID = 0;             
    memset(b->VolLab, 0, sizeof(b->VolLab));
    memset(b->FilSysType, 0, sizeof(b->FilSysType));
    b->HiddSec = 0;             
    b->FSInfo = 0;          

}

uint32_t cluster_to_sector(BPD *b, uint32_t cluster) {
    uint32_t first_data_sector = b->RsvdSecCnt + (b->NumFATs * b->FATSz32);
    return first_data_sector + (cluster - 2) * b->SecPerClus;
}

void read_cluster(FILE *img, BPB *b, uint32_t cluster, unsigned char *buf) {
    uint32_t sector = cluster_to_sector(b, cluster);
    uint32_t byte_offset = sector * b->BytesPerSec;
    uint32_t cluster_size = b->BytesPerSec * b->SecPerClus;

    fseek(img, byte_offset, SEEK_SET);
    fread(buf, 1, cluster_size, img);
}

void write_cluster(FILE *img, BPB *b, uint32_t cluster, unsigned char *buf) {
    uint32_t sector = cluster_to_sector(b, cluster);
    uint32_t byte_offset = sector * b->BytesPerSec;
    uint32_t cluster_size = b->BytesPerSec * b->SecPerClus;

    fseek(img, byte_offset, SEEK_SET);
    fwrite(buf, 1, cluster_size, img);
    fflush(img);
}

uint32_t read_fat_entry(FILE *img, BPB *b, uint32_t cluster) {
    uint32_t fat_offset = b->RsvdSecCnt * b->BytesPerSec + cluster * 4;
    uint32_t entry;

    fseek(img, fat_offset, SEEK_SET);
    fread(&entry, sizeof(uint32_t), 1, img);

    return entry & 0x0FFFFFFF;
}

void write_fat_entry(FILE *img, BPB *b, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = b->RsvdSecCnt * b->BytesPerSec + cluster * 4;

    uint32_t existing;
    fseek(img, fat_offset, SEEK_SET);
    fread(&existing, sizeof(uint32_t), 1, img);

    value = (existing & 0xF0000000) | (value & 0x0FFFFFFF);

    fseek(img, fat_offset, SEEK_SET);
    fwrite(&value, sizeof(uint32_t), 1, img);
    fflush(img);
}


uint32_t find_free_cluster(FILE *img, BPB *b) {
    uint32_t total_clusters = (b->TotSec32 - (b->RsvdSecCnt + b->NumFATs * b->FATSz32)) / b->SecPerClus;

    for (uint32_t cluster = 2; cluster < total_cluster + 2; cluster++) {
        if (read_fat_entry(img, b, cluster) == 0) {
            return cluster;
        }
    }
    return 0;
}
