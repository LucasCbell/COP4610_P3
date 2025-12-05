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

// calculate the byte offset for a cluster
unsigned int get_cluster_offset(BPB *b, unsigned int cluster) {
    // data region starts after reserved sectors and FATs
    // byte offset = (RsvdSecCnt * BytesPerSec) + (NumFATs * FATSz32 * BytesPerSec)
    //             + ((cluster - 2) * SecPerClus * BytesPerSec)
    unsigned int data_region_start = (b->RsvdSecCnt * b->BytesPerSec) + (b->NumFATs * b->FATSz32 * b->BytesPerSec);
    unsigned int cluster_offset = data_region_start + ((cluster - 2) * b->SecPerClus * b->BytesPerSec);
    return cluster_offset;
}

// check if an entry is a dir
int is_directory(dir_entry *entry) {
    return (entry->attr & ATTR_DIRECTORY) != 0;
}

// check if an entry is a long filename entry
int is_longname(dir_entry *entry) {
    return (entry->attr & ATTR_LONG_NAME) == ATTR_LONG_NAME;
}

// trim trailing spaces from filename (FAT32 uses space padding)
char* trim_filename(char *filename, int name_len) {
    char *trimmed = (char *)malloc(name_len + 1);
    int i = name_len - 1;
    
    // find last non-space character
    while (i >= 0 && filename[i] == ' ') {
        i--;
    }
    
    // copy trimmed string
    strncpy(trimmed, filename, i + 1);
    trimmed[i + 1] = '\0';
    
    return trimmed;
}

// read all dir entries from a cluster
dir_entry* read_dir(FILE* img, BPB *b, unsigned int cluster, int *entry_count) {
    unsigned int offset = get_cluster_offset(b, cluster);
    unsigned int cluster_size = b->SecPerClus * b->BytesPerSec;
    
    // calculate max entries in cluster (each entry is 32 bytes)
    int max_entries = cluster_size / sizeof(dir_entry);
    
    // allocate space for entries
    dir_entry *temp_entries = (dir_entry *)malloc(max_entries * sizeof(dir_entry));
    dir_entry *entries = (dir_entry *)malloc(max_entries * sizeof(dir_entry));
    
    if (temp_entries == NULL || entries == NULL) {
        printf("ERROR: Failed to allocate memory for directory entries.\n");
        if (temp_entries) free(temp_entries);
        if (entries) free(entries);
        *entry_count = 0;
        return NULL;
    }
    
    // seek to cluster position and read entries
    fseek(img, offset, SEEK_SET);
    int count = 0;
    
    for (int i = 0; i < max_entries; i++) {
        size_t bytes_read = fread(&temp_entries[i], sizeof(dir_entry), 1, img);
        if (bytes_read != 1) {
            break;
        }
        
        // stop at first free entry (name[0] == 0x00)
        if (temp_entries[i].name[0] == 0x00) {
            break;
        }
        
        // skip deleted entries (name[0] == 0xE5)
        if (temp_entries[i].name[0] == 0xE5) {
            continue;
        }
        
        // skip long filename entries
        if (is_longname(&temp_entries[i])) {
            continue;
        }
        
        // copy valid entry to result array
        memcpy(&entries[count], &temp_entries[i], sizeof(dir_entry));
        count++;
    }
    
    free(temp_entries);
    *entry_count = count;
    return entries;
}

// get the next cluster in the chain from the FAT
unsigned int get_next_cluster(FILE* img, BPB *b, unsigned int cluster) {
    if (cluster >= 0x0FFFFFF8) {
        // end of cluster chain
        return 0;
    }
    
    // FAT starts at RsvdSecCnt * BytesPerSec
    unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec;
    
    // each FAT entry is 4 bytes (32-bit for FAT32)
    // position in FAT for our cluster: fat_offset + (cluster * 4)
    unsigned int entry_offset = fat_offset + (cluster * 4);
    
    // save current position
    long original_pos = ftell(img);
    
    // seek to FAT entry and read it
    fseek(img, entry_offset, SEEK_SET);
    unsigned char fat_entry[4];
    if (fread(fat_entry, 4, 1, img) != 1) {
        fseek(img, original_pos, SEEK_SET);
        return 0;
    }
    
    // convert little-endian FAT entry to cluster number
    unsigned int next_cluster = fat_entry[0] | (fat_entry[1] << 8) | 
                                 (fat_entry[2] << 16) | (fat_entry[3] << 24);
    
    // only keep lower 28 bits (FAT32 uses bits 0-27, bits 28-31 are reserved)
    next_cluster &= 0x0FFFFFFF;
    
    // restore original position
    fseek(img, original_pos, SEEK_SET);
    
    return next_cluster;
}

// get root cluster from BPB
unsigned int get_root_cluster(BPB *b) { return b->RootClus; }

// find a specific dir entry by name in a single cluster
dir_entry* find_entry_in_cluster(FILE* img, BPB *b, unsigned int cluster, char* name) {
    int entry_count = 0;
    dir_entry* entries = read_dir(img, b, cluster, &entry_count);
    
    if (entries == NULL || entry_count == 0) {
        if (entries) free(entries);
        return NULL;
    }
    
    // search for matching entry
    for (int i = 0; i < entry_count; i++) {
        // skip volume labels
        if ((entries[i].attr & ATTR_VOLUME_ID) != 0) {
            continue;
        }
        
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if (strcmp(trimmed, name) == 0) {
            // found it - allocate new entry and return
            dir_entry* result = (dir_entry*)malloc(sizeof(dir_entry));
            memcpy(result, &entries[i], sizeof(dir_entry));
            free(trimmed);
            free(entries);
            return result;
        }
        free(trimmed);
    }
    
    free(entries);
    return NULL;
}

// read all dir entries following the cluster chain
dir_entry* read_dir_chain(FILE* img, BPB *b, unsigned int cluster, int *entry_count) {
    // allocate initial space for entries (start with reasonable size)
    int max_entries = 256;  // should be enough for most directories
    dir_entry* all_entries = (dir_entry*)malloc(max_entries * sizeof(dir_entry));
    
    if (all_entries == NULL) {
        printf("ERROR: Failed to allocate memory for directory chain.\n");
        *entry_count = 0;
        return NULL;
    }
    
    int total_count = 0;
    unsigned int current_cluster = cluster;
    
    // follow cluster chain
    while (current_cluster != 0 && current_cluster < 0x0FFFFFF8) {
        int cluster_entry_count = 0;
        dir_entry* cluster_entries = read_dir(img, b, current_cluster, &cluster_entry_count);
        
        if (cluster_entries == NULL) {
            break;
        }
        
        // copy entries from this cluster
        for (int i = 0; i < cluster_entry_count; i++) {
            if (total_count >= max_entries) {
                // need to expand
                max_entries *= 2;
                dir_entry* temp = (dir_entry*)realloc(all_entries, max_entries * sizeof(dir_entry));
                if (temp == NULL) {
                    free(cluster_entries);
                    *entry_count = total_count;
                    return all_entries;  // return what we have
                }
                all_entries = temp;
            }
            memcpy(&all_entries[total_count], &cluster_entries[i], sizeof(dir_entry));
            total_count++;
        }
        
        free(cluster_entries);
        
        // get next cluster in chain
        current_cluster = get_next_cluster(img, b, current_cluster);
    }
    
    *entry_count = total_count;
    return all_entries;
}
