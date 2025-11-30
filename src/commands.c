#include "common.h"

void info(BPB * b) {
    printf("%-12s %-12d\n", "BytesPerSec:",  b->BytesPerSec);
    printf("%-12s %-12d\n", "SecPerClus:",   b->SecPerClus);
    printf("%-12s %-12d\n", "RsvdSecCnt:",  b->RsvdSecCnt);
    printf("%-12s %-12d\n", "NumFATs:",     b->NumFATs);
    printf("%-12s %-12d\n", "TotalSec32:",  b->TotSec32);
    printf("%-12s %-12d\n", "FATSz32:",     b->FATSz32);
    printf("%-12s %-12d\n", "RootClus:",    b->RootClus);
}

void ls(FILE* img, BPB* b, unsigned int cluster) {
    int entry_count = 0;
    // use cluster chain reading to handle multi cluster dir
    dir_entry* entries = read_dir_chain(img, b, cluster, &entry_count);
    
    if (entries == NULL) {
        printf("ERROR: Could not read directory.\n");
        return;
    }
    
    if (entry_count == 0) {
        printf("ERROR: No entries found in directory.\n");
        free(entries);
        return;
    }
    
    // print all entries
    for (int i = 0; i < entry_count; i++) {
        // skip volume label entries
        if ((entries[i].attr & ATTR_VOLUME_ID) != 0) {
            continue;
        }
        
        // trim filename and print
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        printf("%s\n", trimmed);
        free(trimmed);
    }
    
    free(entries);
}

unsigned int cd(FILE* img, BPB* b, unsigned int current_cluster, char* dirname) {
    // special case: .. (parent dir)
    if (strcmp(dirname, "..") == 0) {
        // look for the ".." entry to get parent cluster
        dir_entry* entry = find_entry_in_cluster(img, b, current_cluster, "..");
        if (entry != NULL) {
            unsigned int parent_cluster = (entry->fstclushi << 16) | entry->fstcluslo;
            free(entry);
            
            // if parent cluster is 0, we're at root - stay at root
            if (parent_cluster == 0) {
                parent_cluster = get_root_cluster(b);
            }
            
            return parent_cluster;
        } else {
            // no ".." entry found, try to stay at current
            printf("ERROR: Cannot go to parent directory.\n");
            return 0;
        }
    }
    
    int entry_count = 0;
    // use cluster chain reading to handle multi-cluster dir
    dir_entry* entries = read_dir_chain(img, b, current_cluster, &entry_count);
    
    if (entries == NULL) {
        printf("ERROR: Could not read directory.\n");
        return 0;
    }
    
    // search for matching dir entry
    for (int i = 0; i < entry_count; i++) {
        // skip volume labels
        if ((entries[i].attr & ATTR_VOLUME_ID) != 0) {
            continue;
        }
        
        // get trimmed filename
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        
        // check if name matches (case-sensitive)
        if (strcmp(trimmed, dirname) == 0) {
            // check if it's a dir
            if (is_directory(&entries[i])) {
                unsigned int new_cluster = (entries[i].fstclushi << 16) | entries[i].fstcluslo;
                free(trimmed);
                free(entries);
                return new_cluster;
            } else {
                // it's a file, not a dir
                printf("ERROR: %s is not a directory.\n", dirname);
                free(trimmed);
                free(entries);
                return 0;
            }
        }
        free(trimmed);
    }
    
    // dir not found
    printf("ERROR: %s does not exist.\n", dirname);
    free(entries);
    return 0;
}