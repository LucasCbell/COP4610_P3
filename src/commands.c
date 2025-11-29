#include "common.h"
#include <ctype.h>

void info(BPB * b) {
    printf("%-12s %-12d\n", "BytesPerSec:",  b->BytesPerSec);
    printf("%-12s %-12d\n", "SecPerClus:",   b->SecPerClus);
    printf("%-12s %-12d\n", "RsvdSecCnt:",  b->RsvdSecCnt);
    printf("%-12s %-12d\n", "NumFATs:",     b->NumFATs);
    printf("%-12s %-12d\n", "TotalSec32:",  b->TotSec32);
    printf("%-12s %-12d\n", "FATSz32:",     b->FATSz32);
    printf("%-12s %-12d\n", "RootClus:",    b->RootClus);
}


void format_name_83(const char *name, unsigned char *dest) {
    memset(dest, ' ', 11);

    int i = 0;
    while (name[i] && i < 11) {
        dest[i] = toupper((unsinged char)name[i]);
        i++;
    }
}

int name_exists_in_dir(FILE *img, BPB *b, uint32_t dir_cluster, const char *name) {
    unsigned char formatted[11];
    format_name_83(name, formatted);

    uint32_t cluster_size = b->BytesPerSec * b->SecPerClus;
    unsigned char *buf = malloc(cluster_size);
    uint32_t cluster = dir_cluster;

    while (cluser < 0x0FFFFFF8) {
        read_cluster(img, b, cluster, buf);

        for (uint32_t i = 0; i < cluster_size; i += 32) {
            DirEntry *entry = (DirEntry *)(buf + i);

            if (entry->DIR_Name[0] == 0x00) {
                free(buf);
                return 0;
            }


            if (entry->DIR_Name[0] = 0xE5) continue;
            if ((entry->DIR_Attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;

            if (memcmp(entry->DIR_Name, formatted, 11) == 0) {
                free(buf);
                return 1;
            }
        }

        cluster = read_fat_entry(img, b, cluster);
    }

    free(buf);
    return 0;
}

int find_free_entry_offset(unsigned char *buf, uint32_t cluster_size) {
    for (uint32_t i = 0; i < cluster_size; i += 32) {
        DirEntry *entry = (DirEntry *)(buf + i);
        if (entry->DIR_Name[0] == 0x00 || entry->DIR_Name[0] == 0xE5) {
            return i;
        }
    }
    return -1;
}

void fat32_mkdir(FILE *img, BPB *b, ShellState *state, const char *dirname) {
    if (strlen(dirname) > 11 || strlen(dirname) == 0) {
        printf("Invalid directory name\n");
        return;
    }

    if (name_exists_in_dir(img, b, state-> current_cluster, dirname)) {
    printf("Directory or file '%s' already existing\n", dirname);
    return;
}

    uint32_t new_cluster = find_free_cluster(img, b);
    if (new_cluster == 0) {
        printf("No free clusters available\n");
        return;
    }

    write_fat_entry(img, b, new_cluster, 0x0FFFFFF8);

    uint32_t cluster_size = b->BytesPerSec * b->SecPerClus;
    unsigned char *new_dir_buf = calloc(1, cluster_size);

    DirEntry *dot = (DirEntry *)new_dir_buf;
    memset(dot->DIR_Name, ' ', 11);
    dot->DIR_Name[0] = '.';
    dot->DIR_Attr = ATTR_DIRECTORY;
    dot->DIR_FstClusHI = (new_cluster >> 16) & 0xFFFF;
    dot->DIR_FstClusLO = new_cluster & 0xFFFF;
    dot->DIR_FileSize = 0;

    DirEntry *dotdot = (DirEntry *)(new_dir_buf + 32);
    memset(dotdot->DIR_Name, ' ', 11);
    dotdot->DIR_Name[0] = '.';
    dotdot->DIR_Name[1] = '.';
    dotdot->DIR_Attr = ATTR_DIRECTORY;
    uint32_t parent = state->current_cluster;
    if (parent == b->RootClus) parent = 0;
    dotdot->DIR_FstClusHI = (parent >> 16) & 0xFFFF;
    dotdot->DIR_FstClusLO = parent & 0xFFFF;
    dotdot->DIR_FileSize = 0;

    write_cluster(img, b, new_cluster, new_dir_buf);
    free(new_dir_buf);

    unsigned char *cur_dir_buf = malloc(cluster_size);
    uint32_t cluster = state->current_cluster;

    while (cluster < 0x0FFFFFF8) {
        read_cluster(img, b, cluster, cur_dir_buf);
        int offset = find_free_entry_offset(cur_dir_buf, cluster_size);

        if (offset >= 0) {

            DirEntry *new_entry = (DirEntry *)(cur_dir_buf + offset);
            memset(new_entry, 0, 32);
            format_name_83(dirname, new_entry->DIR_Name);
            new_entry->DIR_Attr = ATTR_DIRECTORY;
            new_entry->DIR_FstClusHI = (new_cluster >> 16) & 0xFFFF;
            new_entry->DIR_FstClusLO = new_cluster & 0xFFFF;
            new_entry->DIR_FileSize = 0;

            write_cluster(img, b, cluster, cur_dir_buf);
            free(cur_dir_buf);
            return;
        }

        uint32_t next = read_fat_entry(img, b, cluster);
        if (next >= 0x0FFFFFF8) {
            uint32_t ext_cluster = find_free_cluster(img, b);
            if (ext_cluster == 0) {
                printf("No free clusters available\n");
                free(cur_dir_buf);
                return;
            }
            write_fat_entry(img, b, cluster, ext_cluster);
            write_fat_entry(img, b, ext_cluster, 0x0FFFFFF8);

            memset(cur_dir_buf, 0, cluster_size);
            DirEntry *new_entry = (DirEntry *)cur_dir_buf;
            format_name_83(dirname, new_entry->DIR_Name);
            new_entry->DIR_Attr = ATTR_DIRECTORY;
            new_entry->DIR_FstClusHI = (new_cluster >> 16) & 0xFFFF;
            new_entry->DIR_FstClusLO = new_cluster & 0xFFFF;
            new_entry->DIR_FileSize = 0;

            write_cluster(img, b, ext_cluster, cur_dir_buf);
            free(cur_dir_buf);
            return;
        }

        cluster = next;
    }
    free(cur_dir_buf);
}

void fat32_creat(FILE *img, BPB *b, ShellState *state, const char *filename) {

    if (strlen(filename) > 11 || strlen(filename) == 0) {
        printf("Error: Invalid file name\n");
        return;
    }
    
    // Check if name already exists
    if (name_exists_in_dir(img, b, state->current_cluster, filename)) {
        printf("Error: Directory or file '%s' already exists\n", filename);
        return;
    }
    
    // Find free entry in current directory and add the file
    uint32_t cluster_size = b->BytesPerSec * b->SecPerClus;
    unsigned char *buf = malloc(cluster_size);
    uint32_t cluster = state->current_cluster;
    
    while (cluster < 0x0FFFFFF8) {
        read_cluster(img, b, cluster, buf);
        int offset = find_free_entry_offset(buf, cluster_size);
        
        if (offset >= 0) {
            DirEntry *new_entry = (DirEntry *)(buf + offset);
            memset(new_entry, 0, 32);
            format_name_83(filename, new_entry->DIR_Name);
            new_entry->DIR_Attr = ATTR_ARCHIVE;  // Normal file
            new_entry->DIR_FstClusHI = 0;        // No cluster (0-byte file)
            new_entry->DIR_FstClusLO = 0;
            new_entry->DIR_FileSize = 0;
            
            write_cluster(img, b, cluster, buf);
            free(buf);
            return;
        }
        
        uint32_t next = read_fat_entry(img, b, cluster);
        if (next >= 0x0FFFFFF8) {
            // Extend directory
            uint32_t ext_cluster = find_free_cluster(img, b);
            if (ext_cluster == 0) {
                printf("Error: No free clusters available\n");
                free(buf);
                return;
            }
            write_fat_entry(img, b, cluster, ext_cluster);
            write_fat_entry(img, b, ext_cluster, 0x0FFFFFF8);
            
            memset(buf, 0, cluster_size);
            DirEntry *new_entry = (DirEntry *)buf;
            format_name_83(filename, new_entry->DIR_Name);
            new_entry->DIR_Attr = ATTR_ARCHIVE;
            new_entry->DIR_FstClusHI = 0;
            new_entry->DIR_FstClusLO = 0;
            new_entry->DIR_FileSize = 0;
            
            write_cluster(img, b, ext_cluster, buf);
            free(buf);
            return;
        }
        cluster = next;
    }
    
    free(buf);
}
