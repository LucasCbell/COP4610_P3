#include "common.h"
#include "shell.h"

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

void open(char* filename, char* flags, FILE* img, BPB* b, unsigned int current_cluster, file_table* table, char* img_name){

    // check for invalid flag input
    if (strcmp(flags, "-r") != 0 && strcmp(flags, "-w") != 0 && 
        strcmp(flags, "-wr") != 0 && strcmp(flags, "-rw") != 0) {

        printf("Error: invalid flags\n");
        return;
    }
    // check if file is open in filetable
    for(int i = 0; i < 10; i++){
        if(table[i].isopen == 1 && strcmp(table[i].filename, filename) == 0){
            printf("Error, file already open");
            return;
        }
    }
    int entry_count = 0;
    dir_entry* temp_entry = read_dir_chain(img, b, current_cluster, &entry_count);
    if(temp_entry == NULL){ 
        printf("Error: cannot read directory\n");
        return; 
    }
    dir_entry* entry = NULL;
    for (int i = 0; i < entry_count; i++) {
        if (is_longname(&temp_entry[i])) {
            continue;
        }
        char* trimmed = trim_filename((char*)temp_entry[i].name, 11);
        if (strcmp(trimmed, filename) == 0) {
            entry = &temp_entry[i];
            free(trimmed);
            break;
        }
        free(trimmed);
    }

    if (entry == NULL){
        printf("File doesnt exist\n");
        free(temp_entry);
        return;
    }

     // check if its a dir
    if(is_directory(entry)){
        printf("Error: %s is a directory\n", filename);
        free(entry);
        return;
    }
    // finding lowest file index then populating it
    int index = -1; 
    for(int i = 0; i < 10; i++){
        if(table[i].isopen == 0){
            index = i;
            break;
        }
    }
    if(index == -1) {
        printf("Error, no more file space");
        return;
    }
    strcpy(table[index].filename, filename);
    char path[512];
    strcpy(path, img_name);
    strcat(path, current_path);
    int path_len = strlen(path);
    if(path_len > 0 && path[path_len - 1] == '/'){
        path[path_len - 1] = '\0';
    }
    strcpy(table[index].path, path);

    if(strcmp(flags, "-r") == 0){
        table[index].mode = 'r';
    } else if(strcmp(flags, "-w") == 0){
        table[index].mode = 'w';
    }else{
         table[index].mode = 'a';           //a if its both 
    }

    table[index].offset = 0;
    table[index].filesize = entry->filesize;
    table[index].fp = img;
    table[index].index = index;
    table[index].isopen = 1;
    
    printf("Opened \n");
    free(temp_entry);

}

void close(char* filename, file_table* table){

    int index = -1;
    for(int i = 0; i < 10; i++){
        if(table[i].isopen == 1 && strcmp(table[i].filename, filename) == 0){
            index = i;
            break;
        }
    }
    if (index == -1){
        printf("Error: file doesn't exist or not open");
        return;
    }

    memset(table[index].filename, 0 , 256);
    memset(table[index].path, 0 , 512);
    table[index].mode = 0;
    table[index].offset = 0;
    table[index].filesize = 0;
    table[index].fp = NULL;
    table[index].index = index;
    table[index].isopen = 0;

    printf("Closed \n");
}

void lsof(file_table* table){

    int found = 0;

    for(int i = 0; i < 10; i++){
        if(table[i].isopen == 1){
            found = 1;
            break;
        }
    }

    if(!found){
        printf("No files are opened\n");
        return;
    }

    printf("%-6s %-12s %-6s %-8s %s\n", "INDEX", "FILENAME", "MODE", "OFFSET", "PATH");
    printf("%-6s %-12s %-6s %-8s %s\n", "-----", "--------", "----", "------", "----");

    for (int i = 0; i < 10; i++) {
        if (table[i].isopen == 1) {
            char mode_str[5];
            if (table[i].mode == 'r') {
                strcpy(mode_str, "-r");
            } else if (table[i].mode == 'w') {
                strcpy(mode_str, "-w");
            } else {
                strcpy(mode_str, "-rw");
            }
            
            printf("%-6d %-12s %-6s %-8d %s\n", 
                   table[i].index, 
                   table[i].filename, 
                   mode_str, 
                   table[i].offset, 
                   table[i].path);
        }
    }
}

void lseek(char* filename, int offset, file_table* table) {
    
    int index = -1;
    for(int i = 0; i < 10; i++){
        if(table[i].isopen == 1 && strcmp(table[i].filename, filename) == 0){
            index = i;
            break;
        }
    }
    if(index == -1){
        printf("Error: file doesnt exist or not opened\n");
        return;
    }

    if(offset > table[index].filesize) {
        printf("Error: offset larger than file size\n");
        table[index].offset = table[index].filesize;
        return;
    }
    if(offset < 0){
        printf("Error: offset cannot be negative\n");
        return;
    }

    table[index].offset = offset;
}

void read(char* filename, int size, FILE* img, BPB* b, file_table* table, unsigned int current_cluster){
    
    // find file in table and check if opened for reading
    int index = -1;
    for(int i = 0; i < 10; i++){
        if(table[i].isopen == 1 && strcmp(table[i].filename, filename) == 0){
            index = i;
            break;
        }
    }
    
    if(index == -1){
        printf("Error: file does not exist or is not open\n");
        return;
    }
    
    // check if file is opened for reading
    if(table[index].mode != 'r' && table[index].mode != 'a'){
        printf("Error: file is not open for reading\n");
        return;
    }
    
    // get file info from table
    int offset = table[index].offset;
    int filesize = table[index].filesize;
    
    // check if already at end of file
    if(offset >= filesize){
        printf("Error: already at end of file\n");
        return;
    }
    
    // calculate how many bytes to actually read
    int bytes_to_read = size;
    if(offset + size > filesize){
        bytes_to_read = filesize - offset;
    }
    
    // find the file's first cluster from directory
    int entry_count = 0;
    dir_entry* entries = read_dir_chain(img, b, current_cluster, &entry_count);
    if(entries == NULL){
        printf("Error: could not read directory\n");
        return;
    }
    
    dir_entry* file_entry = NULL;
    for(int i = 0; i < entry_count; i++){
        if(is_longname(&entries[i])) continue;
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if(strcmp(trimmed, filename) == 0){
            file_entry = &entries[i];
            free(trimmed);
            break;
        }
        free(trimmed);
    }
    
    if(file_entry == NULL){
        printf("Error: file not found in directory\n");
        free(entries);
        return;
    }
    
    // get files first cluster
    unsigned int file_cluster = (file_entry->fstclushi << 16) | file_entry->fstcluslo;
    
    // calc cluster size
    unsigned int cluster_size = b->BytesPerSec * b->SecPerClus;
    
    // navigate to the cluster containing the offset
    unsigned int cluster_offset = offset;  // offset within current cluster
    unsigned int current_file_cluster = file_cluster;
    
    // skip clusters until we reach the one containing our offset
    while(cluster_offset >= cluster_size){
        current_file_cluster = get_next_cluster(img, b, current_file_cluster);
        if(current_file_cluster >= 0x0FFFFFF8){
            printf("Error: offset beyond file data\n");
            free(entries);
            return;
        }
        cluster_offset -= cluster_size;
    }
    
    // read and print the data
    int bytes_read = 0;
    unsigned char* buffer = malloc(bytes_to_read + 1);
    if(buffer == NULL){
        printf("Error: memory allocation failed\n");
        free(entries);
        return;
    }
    
    while(bytes_read < bytes_to_read && current_file_cluster < 0x0FFFFFF8){
        // calc position in image file
        unsigned int cluster_start = get_cluster_offset(b, current_file_cluster);
        
        // how many bytes to read from this cluster
        unsigned int bytes_in_cluster = cluster_size - cluster_offset;
        if(bytes_in_cluster > (unsigned int)(bytes_to_read - bytes_read)){
            bytes_in_cluster = bytes_to_read - bytes_read;
        }
        
        // seek to position and read
        fseek(img, cluster_start + cluster_offset, SEEK_SET);
        fread(buffer + bytes_read, 1, bytes_in_cluster, img);
        
        bytes_read += bytes_in_cluster;
        cluster_offset = 0;  // subsequent clusters start at offset 0
        
        // move to next cluster if needed
        if(bytes_read < bytes_to_read){
            current_file_cluster = get_next_cluster(img, b, current_file_cluster);
        }
    }
    
    // null-terminate and print
    buffer[bytes_read] = '\0';
    printf("%s\n", buffer);
    
    // update the file offset
    table[index].offset = offset + bytes_read;
    
    free(buffer);
    free(entries);
}