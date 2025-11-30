#pragma once

#include <stdio.h>

typedef struct __attribute__((packed)){
    // these are taken from the Boot Sector section of the
    // FAT32 documentation

    // char     1 byte
    // short    2 bytes
    // int      4 bytes

    // use char[] for:
    //  - raw byte sequences
    //  - strings that aren't terminated
    //  - filenames
    //  - any fields that are defined as bytes[]

    // useful bits:
    unsigned short BytesPerSec;     // count o bytes per sector (required)
    unsigned char SecPerClus;       // # sectors per allocation unit (required)
    unsigned short RsvdSecCnt;      // # of reserved sectors (required)
    unsigned char NumFATs;          // how many FAT data structures (required)
    unsigned int TotSec32;          // 32-bit total count of sectors on volume (required)
    unsigned int FATSz32;           // 32-bit count of sectors occupied by one FAT (required)
    unsigned int RootClus;          // cluster # of first cluster in root directory (required)

    // uselss bits:
    unsigned char jmpBoot[3];       // jump instruction to boot code (useless)
    unsigned char OEMName[8];       // (string) what system formatted the volume (useless)
    unsigned short RootEntCnt;      // set to 0 for FAT32 (useless)
    unsigned short TotSec16;        // set to 0 for FAT32 (useless)
    unsigned char Media;            // type of storage (useless)
    unsigned short FATSz16;         // set to 0 for FAT32 (useless)
    unsigned short SecPerTrk;       // sectors per track for interrupt (useless)
    unsigned short NumHeads;        // number of heads for interrupt (useless)
    unsigned short ExtFlags;        // flags for FAT settings (useless)
    unsigned short FSVer;           // version number of FAT32 (useless)
    unsigned short BkBootSec;       // indicates sector # in reserved area of copy? (useless)
    unsigned char Reserved[12];     // reserved area for future use (useless)
    unsigned char DrvNum;           // driver number (useless)
    unsigned char Reserved1;        // reserved by Windows (useless)
    unsigned char BootSig;          // extended boot signature (useless)
    unsigned int VolID;             // volume serial number (useless)
    unsigned char VolLab[11];       // volume label (useless)
    unsigned char FilSysType[8];    // always set to FAT32 (useless)

    // not entirely sure
    unsigned int HiddSec;           // hidden sectors after parition (probably useless)    
    unsigned short FSInfo;          // sector # of FSINFO in BackupBoot (probably useless)

    // byes 510 and 511 contain "0xAA55"
    // BPB is 90 bytes long
} BPB;

typedef struct __attribute__((packed)) {
    unsigned char name[11];         // 8.3 filename format (8 chars + 3 extension)
    unsigned char attr;             // file attributes (bit 4 = directory)
    unsigned char ntres;            // reserved for use by Windows NT
    unsigned char crttimetenth;     // creation time (tenths of a second)
    unsigned short crttime;         // ceation time
    unsigned short crtdate;         // creation date
    unsigned short lstaccdate;      // last access date
    unsigned short fstclushi;       // high word of first cluster (FAT32)
    unsigned short wrttime;         // last write time
    unsigned short wrtdate;         // last write date
    unsigned short fstcluslo;       // low word of first cluster
    unsigned int filesize;          // file size in bytes
} dir_entry;

// dir entry attribute bits
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  0x0F

void read_boot_sector(FILE* img, unsigned char* boot_sector);
void parse_boot_sector(BPB *b, unsigned char* boot_sector);
dir_entry* read_dir(FILE* img, BPB *b, unsigned int cluster, int *entry_count);
dir_entry* read_dir_chain(FILE* img, BPB *b, unsigned int cluster, int *entry_count);
unsigned int get_cluster_offset(BPB *b, unsigned int cluster);
unsigned int get_next_cluster(FILE* img, BPB *b, unsigned int cluster);
dir_entry* find_entry_in_cluster(FILE* img, BPB *b, unsigned int cluster, char* name);
unsigned int get_root_cluster(BPB *b);
int is_directory(dir_entry *entry);
int is_longname(dir_entry *entry);
char* trim_filename(char *filename, int name_len);