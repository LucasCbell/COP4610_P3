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

static void format_name_83(const char *name, unsigned char *dest) {
    memset(dest, ' ', 11);
    int i = 0;
    while (name[i] && i < 11) {
        dest[i] = toupper((unsigned char)name[i]);
        i++;
    }
}

static int name_exists_in_dir(FILE *img, BPB *b, unsigned int dir_cluster, const char *name) {
    char upper_name[12];
    int i = 0;
    while (name[i] && i < 11) {
        upper_name[i] = toupper((unsigned char)name[i]);
        i++;
    }
    upper_name[i] = '\0';

    int entry_count = 0;
    dir_entry *entries = read_dir_chain(img, b, dir_cluster, &entry_count);
    
    if (entries == NULL) {
        return 0;
    }

    for (int j = 0; j < entry_count; j++) {
        if ((entries[j].attr & ATTR_VOLUME_ID) != 0) {
            continue;
        }

        char *trimmed = trim_filename((char *)entries[j].name, 11);
        if (strcmp(trimmed, upper_name) == 0) {
            free(trimmed);
            free(entries);
            return 1;
        }
        free(trimmed);
    }

    free(entries);
    return 0;
}

static unsigned int find_free_cluster_local(FILE *img, BPB *b) {
    unsigned int data_sectors = b->TotSec32 - (b->RsvdSecCnt + b->NumFATs * b->FATSz32);
    unsigned int total_clusters = data_sectors / b->SecPerClus;

    for (unsigned int cluster = 2; cluster < total_clusters + 2; cluster++) {
        unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + cluster * 4;
        
        fseek(img, fat_offset, SEEK_SET);
        unsigned int entry;
        fread(&entry, sizeof(unsigned int), 1, img);
        entry &= 0x0FFFFFFF;
        
        if (entry == 0) {
            return cluster;
        }
    }

    return 0;
}


static void write_fat_entry_local(FILE *img, BPB *b, unsigned int cluster, unsigned int value) {
    unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + cluster * 4;

    unsigned int existing;
    fseek(img, fat_offset, SEEK_SET);
    fread(&existing, sizeof(unsigned int), 1, img);

    value = (existing & 0xF0000000) | (value & 0x0FFFFFFF);

    fseek(img, fat_offset, SEEK_SET);
    fwrite(&value, sizeof(unsigned int), 1, img);
    fflush(img);
}

static void write_cluster_local(FILE *img, BPB *b, unsigned int cluster, unsigned char *buf) {
    unsigned int offset = get_cluster_offset(b, cluster);
    unsigned int cluster_size = b->SecPerClus * b->BytesPerSec;

    fseek(img, offset, SEEK_SET);
    fwrite(buf, 1, cluster_size, img);
    fflush(img);
}

static unsigned int find_free_dir_entry(FILE *img, BPB *b, unsigned int dir_cluster, unsigned int *out_cluster) {
    unsigned int cluster_size = b->SecPerClus * b->BytesPerSec;
    unsigned int current_cluster = dir_cluster;

    while (current_cluster != 0 && current_cluster < 0x0FFFFFF8) {
        unsigned int offset = get_cluster_offset(b, current_cluster);
        int max_entries = cluster_size / sizeof(dir_entry);

        for (int i = 0; i < max_entries; i++) {
            unsigned int entry_offset = offset + (i * sizeof(dir_entry));
            fseek(img, entry_offset, SEEK_SET);
            
            unsigned char first_byte;
            fread(&first_byte, 1, 1, img);

            if (first_byte == 0x00 || first_byte == 0xE5) {
                *out_cluster = current_cluster;
                return entry_offset;
            }
        }

        current_cluster = get_next_cluster(img, b, current_cluster);
    }

    return 0;
}

void fat32_mkdir(FILE *img, BPB *b, unsigned int current_cluster, const char *dirname) {
    if (dirname == NULL || strlen(dirname) == 0) {
        printf("Error: Directory name required\n");
        return;
    }
    if (strlen(dirname) > 11) {
        printf("Error: Directory name too long (max 11 characters)\n");
        return;
    }

    if (name_exists_in_dir(img, b, current_cluster, dirname)) {
        printf("Error: '%s' already exists\n", dirname);
        return;
    }

    unsigned int new_cluster = find_free_cluster_local(img, b);
    if (new_cluster == 0) {
        printf("Error: No free clusters available\n");
        return;
    }

    write_fat_entry_local(img, b, new_cluster, 0x0FFFFFF8);

    unsigned int cluster_size = b->SecPerClus * b->BytesPerSec;
    unsigned char *new_dir_buf = calloc(1, cluster_size);
    if (!new_dir_buf) {
        printf("Error: Memory allocation failed\n");
        return;
    }

    /* Create "." entry */
    dir_entry *dot = (dir_entry *)new_dir_buf;
    memset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = ATTR_DIRECTORY;
    dot->fstclushi = (new_cluster >> 16) & 0xFFFF;
    dot->fstcluslo = new_cluster & 0xFFFF;
    dot->filesize = 0;

    /* Create ".." entry */
    dir_entry *dotdot = (dir_entry *)(new_dir_buf + sizeof(dir_entry));
    memset(dotdot->name, ' ', 11);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = ATTR_DIRECTORY;
    
    unsigned int parent_cluster = current_cluster;
    if (parent_cluster == get_root_cluster(b)) {
        parent_cluster = 0;
    }
    dotdot->fstclushi = (parent_cluster >> 16) & 0xFFFF;
    dotdot->fstcluslo = parent_cluster & 0xFFFF;
    dotdot->filesize = 0;

    write_cluster_local(img, b, new_cluster, new_dir_buf);
    free(new_dir_buf);

    /* Add entry to current directory */
    unsigned int entry_cluster;
    unsigned int entry_offset = find_free_dir_entry(img, b, current_cluster, &entry_cluster);
    
    if (entry_offset == 0) {
        unsigned int ext_cluster = find_free_cluster_local(img, b);
        if (ext_cluster == 0) {
            printf("Error: No free clusters to extend directory\n");
            return;
        }

        unsigned int last_cluster = current_cluster;
        unsigned int next = get_next_cluster(img, b, last_cluster);
        while (next != 0 && next < 0x0FFFFFF8) {
            last_cluster = next;
            next = get_next_cluster(img, b, last_cluster);
        }

        write_fat_entry_local(img, b, last_cluster, ext_cluster);
        write_fat_entry_local(img, b, ext_cluster, 0x0FFFFFF8);

        unsigned char *clear_buf = calloc(1, cluster_size);
        write_cluster_local(img, b, ext_cluster, clear_buf);
        free(clear_buf);

        entry_offset = get_cluster_offset(b, ext_cluster);
    }

    dir_entry new_entry;
    memset(&new_entry, 0, sizeof(dir_entry));
    format_name_83(dirname, new_entry.name);
    new_entry.attr = ATTR_DIRECTORY;
    new_entry.fstclushi = (new_cluster >> 16) & 0xFFFF;
    new_entry.fstcluslo = new_cluster & 0xFFFF;
    new_entry.filesize = 0;

    fseek(img, entry_offset, SEEK_SET);
    fwrite(&new_entry, sizeof(dir_entry), 1, img);
    fflush(img);
}

void fat32_creat(FILE *img, BPB *b, unsigned int current_cluster, const char *filename) {
    if (filename == NULL || strlen(filename) == 0) {
        printf("Error: Filename required\n");
        return;
    }
    if (strlen(filename) > 11) {
        printf("Error: Filename too long (max 11 characters)\n");
        return;
    }

    if (name_exists_in_dir(img, b, current_cluster, filename)) {
        printf("Error: '%s' already exists\n", filename);
        return;
    }

    unsigned int cluster_size = b->SecPerClus * b->BytesPerSec;
    unsigned int entry_cluster;
    unsigned int entry_offset = find_free_dir_entry(img, b, current_cluster, &entry_cluster);
    
    if (entry_offset == 0) {
        unsigned int ext_cluster = find_free_cluster_local(img, b);
        if (ext_cluster == 0) {
            printf("Error: No free clusters to extend directory\n");
            return;
        }

        unsigned int last_cluster = current_cluster;
        unsigned int next = get_next_cluster(img, b, last_cluster);
        while (next != 0 && next < 0x0FFFFFF8) {
            last_cluster = next;
            next = get_next_cluster(img, b, last_cluster);
        }

        write_fat_entry_local(img, b, last_cluster, ext_cluster);
        write_fat_entry_local(img, b, ext_cluster, 0x0FFFFFF8);

        unsigned char *clear_buf = calloc(1, cluster_size);
        write_cluster_local(img, b, ext_cluster, clear_buf);
        free(clear_buf);

        entry_offset = get_cluster_offset(b, ext_cluster);
    }

    dir_entry new_entry;
    memset(&new_entry, 0, sizeof(dir_entry));
    format_name_83(filename, new_entry.name);
    new_entry.attr = ATTR_ARCHIVE;
    new_entry.fstclushi = 0;
    new_entry.fstcluslo = 0;
    new_entry.filesize = 0;

    fseek(img, entry_offset, SEEK_SET);
    fwrite(&new_entry, sizeof(dir_entry), 1, img);
    fflush(img);
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


void write_file(char* filename, char* string, FILE* img, BPB* b, file_table* table, unsigned int current_cluster) {
    
    // Find file in table and check if opened for writing
    int index = -1;
    for (int i = 0; i < 10; i++) {
        if (table[i].isopen == 1 && strcmp(table[i].filename, filename) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        printf("Error: file does not exist or is not open\n");
        return;
    }
    
    // Check if file is opened for writing ('w' or 'a' for both)
    if (table[index].mode != 'w' && table[index].mode != 'a') {
        printf("Error: file is not open for writing\n");
        return;
    }
    
    // Get file info from table
    int offset = table[index].offset;
    int filesize = table[index].filesize;
    int string_len = strlen(string);
    
    // Find the file's directory entry to get/update cluster info
    int entry_count = 0;
    dir_entry* entries = read_dir_chain(img, b, current_cluster, &entry_count);
    if (entries == NULL) {
        printf("Error: could not read directory\n");
        return;
    }
    
    dir_entry* file_entry = NULL;
    int entry_index = -1;
    for (int i = 0; i < entry_count; i++) {
        if (is_longname(&entries[i])) continue;
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if (strcmp(trimmed, filename) == 0) {
            file_entry = &entries[i];
            entry_index = i;
            free(trimmed);
            break;
        }
        free(trimmed);
    }
    
    if (file_entry == NULL) {
        printf("Error: file not found in directory\n");
        free(entries);
        return;
    }
    
    // Check if it's a directory
    if (is_directory(file_entry)) {
        printf("Error: %s is a directory\n", filename);
        free(entries);
        return;
    }
    
    // Get file's first cluster
    unsigned int file_cluster = (file_entry->fstclushi << 16) | file_entry->fstcluslo;
    unsigned int cluster_size = b->BytesPerSec * b->SecPerClus;
    
    // If file has no cluster allocated (empty file), allocate one
    if (file_cluster == 0) {
        // Find a free cluster
        unsigned int data_sectors = b->TotSec32 - (b->RsvdSecCnt + b->NumFATs * b->FATSz32);
        unsigned int total_clusters = data_sectors / b->SecPerClus;
        
        unsigned int new_cluster = 0;
        for (unsigned int c = 2; c < total_clusters + 2; c++) {
            unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + c * 4;
            fseek(img, fat_offset, SEEK_SET);
            unsigned int fat_entry;
            fread(&fat_entry, sizeof(unsigned int), 1, img);
            fat_entry &= 0x0FFFFFFF;
            if (fat_entry == 0) {
                new_cluster = c;
                break;
            }
        }
        
        if (new_cluster == 0) {
            printf("Error: no free clusters available\n");
            free(entries);
            return;
        }
        
        // Mark cluster as end of chain
        unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + new_cluster * 4;
        unsigned int end_marker = 0x0FFFFFF8;
        fseek(img, fat_offset, SEEK_SET);
        fwrite(&end_marker, sizeof(unsigned int), 1, img);
        fflush(img);
        
        // Update directory entry with new cluster
        file_cluster = new_cluster;
        file_entry->fstclushi = (new_cluster >> 16) & 0xFFFF;
        file_entry->fstcluslo = new_cluster & 0xFFFF;
        
        // Clear the new cluster
        unsigned char* clear_buf = calloc(1, cluster_size);
        unsigned int cluster_offset_pos = get_cluster_offset(b, new_cluster);
        fseek(img, cluster_offset_pos, SEEK_SET);
        fwrite(clear_buf, 1, cluster_size, img);
        fflush(img);
        free(clear_buf);
    }
    
    // Calculate if we need to extend the file
    int new_size = offset + string_len;
    int need_extension = (new_size > filesize);
    
    // If we need more clusters, allocate them
    if (need_extension) {
        // Find last cluster in chain
        unsigned int last_cluster = file_cluster;
        unsigned int next = get_next_cluster(img, b, last_cluster);
        while (next != 0 && next < 0x0FFFFFF8) {
            last_cluster = next;
            next = get_next_cluster(img, b, last_cluster);
        }
        
        // Calculate how many clusters we need total
        int clusters_needed = (new_size + cluster_size - 1) / cluster_size;
        
        // Count current clusters
        int current_clusters = 0;
        unsigned int temp_cluster = file_cluster;
        while (temp_cluster != 0 && temp_cluster < 0x0FFFFFF8) {
            current_clusters++;
            temp_cluster = get_next_cluster(img, b, temp_cluster);
        }
        
        // Allocate additional clusters if needed
        while (current_clusters < clusters_needed) {
            // Find free cluster
            unsigned int data_sectors = b->TotSec32 - (b->RsvdSecCnt + b->NumFATs * b->FATSz32);
            unsigned int total_clusters = data_sectors / b->SecPerClus;
            
            unsigned int new_cluster = 0;
            for (unsigned int c = 2; c < total_clusters + 2; c++) {
                unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + c * 4;
                fseek(img, fat_offset, SEEK_SET);
                unsigned int fat_entry;
                fread(&fat_entry, sizeof(unsigned int), 1, img);
                fat_entry &= 0x0FFFFFFF;
                if (fat_entry == 0) {
                    new_cluster = c;
                    break;
                }
            }
            
            if (new_cluster == 0) {
                printf("Error: no free clusters available\n");
                free(entries);
                return;
            }
            
            // Link last cluster to new cluster
            unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + last_cluster * 4;
            fseek(img, fat_offset, SEEK_SET);
            fwrite(&new_cluster, sizeof(unsigned int), 1, img);
            
            // Mark new cluster as end of chain
            fat_offset = b->RsvdSecCnt * b->BytesPerSec + new_cluster * 4;
            unsigned int end_marker = 0x0FFFFFF8;
            fseek(img, fat_offset, SEEK_SET);
            fwrite(&end_marker, sizeof(unsigned int), 1, img);
            fflush(img);
            
            // Clear new cluster
            unsigned char* clear_buf = calloc(1, cluster_size);
            unsigned int cluster_offset_pos = get_cluster_offset(b, new_cluster);
            fseek(img, cluster_offset_pos, SEEK_SET);
            fwrite(clear_buf, 1, cluster_size, img);
            fflush(img);
            free(clear_buf);
            
            last_cluster = new_cluster;
            current_clusters++;
        }
    }
    
    // Navigate to the cluster containing the offset
    unsigned int cluster_offset = offset;
    unsigned int current_file_cluster = file_cluster;
    
    while (cluster_offset >= cluster_size) {
        current_file_cluster = get_next_cluster(img, b, current_file_cluster);
        if (current_file_cluster >= 0x0FFFFFF8) {
            printf("Error: offset beyond file data\n");
            free(entries);
            return;
        }
        cluster_offset -= cluster_size;
    }
    
    // Write the data
    int bytes_written = 0;
    while (bytes_written < string_len && current_file_cluster < 0x0FFFFFF8) {
        unsigned int cluster_start = get_cluster_offset(b, current_file_cluster);
        
        // How many bytes to write to this cluster
        unsigned int bytes_in_cluster = cluster_size - cluster_offset;
        if (bytes_in_cluster > (unsigned int)(string_len - bytes_written)) {
            bytes_in_cluster = string_len - bytes_written;
        }
        
        // Seek to position and write
        fseek(img, cluster_start + cluster_offset, SEEK_SET);
        fwrite(string + bytes_written, 1, bytes_in_cluster, img);
        fflush(img);
        
        bytes_written += bytes_in_cluster;
        cluster_offset = 0;
        
        if (bytes_written < string_len) {
            current_file_cluster = get_next_cluster(img, b, current_file_cluster);
        }
    }
    
    // Update file size in directory entry if file grew
    if (new_size > filesize) {
        file_entry->filesize = new_size;
        table[index].filesize = new_size;
    }
    
    // Write updated directory entry back to disk
    // Find the directory entry position and update it
    unsigned int dir_cluster = current_cluster;
    int entries_per_cluster = cluster_size / sizeof(dir_entry);
    int entry_num = 0;
    
    // Skip to the correct cluster and entry
    int target_entry = entry_index;
    while (target_entry >= entries_per_cluster) {
        dir_cluster = get_next_cluster(img, b, dir_cluster);
        target_entry -= entries_per_cluster;
    }
    
    unsigned int dir_offset = get_cluster_offset(b, dir_cluster) + (target_entry * sizeof(dir_entry));
    fseek(img, dir_offset, SEEK_SET);
    fwrite(file_entry, sizeof(dir_entry), 1, img);
    fflush(img);
    
    // Update offset in file table
    table[index].offset = offset + string_len;
    
    free(entries);
}


void mv(char* src, char* dest, FILE* img, BPB* b, unsigned int current_cluster, file_table* table) {
    
    // Check if source file is open
    for (int i = 0; i < 10; i++) {
        if (table[i].isopen == 1 && strcmp(table[i].filename, src) == 0) {
            printf("Error: file is open, please close it first\n");
            return;
        }
    }
    
    // Find source entry
    int entry_count = 0;
    dir_entry* entries = read_dir_chain(img, b, current_cluster, &entry_count);
    if (entries == NULL) {
        printf("Error: could not read directory\n");
        return;
    }
    
    dir_entry* src_entry = NULL;
    int src_index = -1;
    for (int i = 0; i < entry_count; i++) {
        if (is_longname(&entries[i])) continue;
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if (strcmp(trimmed, src) == 0) {
            src_entry = &entries[i];
            src_index = i;
            free(trimmed);
            break;
        }
        free(trimmed);
    }
    
    if (src_entry == NULL) {
        printf("Error: %s does not exist\n", src);
        free(entries);
        return;
    }
    
    // Check if destination exists
    dir_entry* dest_entry = NULL;
    int dest_index = -1;
    for (int i = 0; i < entry_count; i++) {
        if (is_longname(&entries[i])) continue;
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if (strcmp(trimmed, dest) == 0) {
            dest_entry = &entries[i];
            dest_index = i;
            free(trimmed);
            break;
        }
        free(trimmed);
    }
    
    unsigned int cluster_size = b->BytesPerSec * b->SecPerClus;
    int entries_per_cluster = cluster_size / sizeof(dir_entry);
    
    if (dest_entry != NULL) {
        // Destination exists - check if it's a directory
        if (!is_directory(dest_entry)) {
            printf("Error: %s is a file, not a directory\n", dest);
            free(entries);
            return;
        }
        
        // Move source into destination directory
        unsigned int dest_cluster = (dest_entry->fstclushi << 16) | dest_entry->fstcluslo;
        
        // Check if file already exists in destination
        int dest_entry_count = 0;
        dir_entry* dest_entries = read_dir_chain(img, b, dest_cluster, &dest_entry_count);
        if (dest_entries != NULL) {
            for (int i = 0; i < dest_entry_count; i++) {
                if (is_longname(&dest_entries[i])) continue;
                char* trimmed = trim_filename((char*)dest_entries[i].name, 11);
                if (strcmp(trimmed, src) == 0) {
                    printf("Error: %s already exists in destination\n", src);
                    free(trimmed);
                    free(dest_entries);
                    free(entries);
                    return;
                }
                free(trimmed);
            }
            free(dest_entries);
        }
        
        // Find free entry in destination directory
        unsigned int dest_dir_cluster = dest_cluster;
        int found_slot = 0;
        unsigned int slot_offset = 0;
        
        while (dest_dir_cluster != 0 && dest_dir_cluster < 0x0FFFFFF8 && !found_slot) {
            unsigned int dir_offset = get_cluster_offset(b, dest_dir_cluster);
            
            for (int i = 0; i < entries_per_cluster; i++) {
                fseek(img, dir_offset + (i * sizeof(dir_entry)), SEEK_SET);
                unsigned char first_byte;
                fread(&first_byte, 1, 1, img);
                
                if (first_byte == 0x00 || first_byte == 0xE5) {
                    slot_offset = dir_offset + (i * sizeof(dir_entry));
                    found_slot = 1;
                    break;
                }
            }
            
            if (!found_slot) {
                dest_dir_cluster = get_next_cluster(img, b, dest_dir_cluster);
            }
        }
        
        if (!found_slot) {
            printf("Error: destination directory is full\n");
            free(entries);
            return;
        }
        
        // Write source entry to destination
        fseek(img, slot_offset, SEEK_SET);
        fwrite(src_entry, sizeof(dir_entry), 1, img);
        fflush(img);
        
        // Mark source entry as deleted (0xE5)
        unsigned int src_dir_cluster = current_cluster;
        int src_target = src_index;
        while (src_target >= entries_per_cluster) {
            src_dir_cluster = get_next_cluster(img, b, src_dir_cluster);
            src_target -= entries_per_cluster;
        }
        
        unsigned int src_offset = get_cluster_offset(b, src_dir_cluster) + (src_target * sizeof(dir_entry));
        unsigned char deleted_marker = 0xE5;
        fseek(img, src_offset, SEEK_SET);
        fwrite(&deleted_marker, 1, 1, img);
        fflush(img);
        
    } else {
        // Destination doesn't exist - rename source to dest
        // Format new name in 8.3 format
        unsigned char new_name[11];
        memset(new_name, ' ', 11);
        int i = 0;
        while (dest[i] && i < 11) {
            new_name[i] = toupper((unsigned char)dest[i]);
            i++;
        }
        
        // Update name in source entry
        memcpy(src_entry->name, new_name, 11);
        
        // Write updated entry back to disk
        unsigned int src_dir_cluster = current_cluster;
        int src_target = src_index;
        while (src_target >= entries_per_cluster) {
            src_dir_cluster = get_next_cluster(img, b, src_dir_cluster);
            src_target -= entries_per_cluster;
        }
        
        unsigned int src_offset = get_cluster_offset(b, src_dir_cluster) + (src_target * sizeof(dir_entry));
        fseek(img, src_offset, SEEK_SET);
        fwrite(src_entry, sizeof(dir_entry), 1, img);
        fflush(img);
    }
    
    free(entries);
}

void rm(char* filename, FILE* img, BPB* b, unsigned int current_cluster, file_table* table) {
    
    // Check if file is open
    for (int i = 0; i < 10; i++) {
        if (table[i].isopen == 1 && strcmp(table[i].filename, filename) == 0) {
            printf("Error: file is open, please close it first\n");
            return;
        }
    }
    
    // Find the file entry
    int entry_count = 0;
    dir_entry* entries = read_dir_chain(img, b, current_cluster, &entry_count);
    if (entries == NULL) {
        printf("Error: could not read directory\n");
        return;
    }
    
    dir_entry* file_entry = NULL;
    int entry_index = -1;
    for (int i = 0; i < entry_count; i++) {
        if (is_longname(&entries[i])) continue;
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if (strcmp(trimmed, filename) == 0) {
            file_entry = &entries[i];
            entry_index = i;
            free(trimmed);
            break;
        }
        free(trimmed);
    }
    
    if (file_entry == NULL) {
        printf("Error: %s does not exist\n", filename);
        free(entries);
        return;
    }
    
    // Check if it's a directory
    if (is_directory(file_entry)) {
        printf("Error: %s is a directory, use rmdir instead\n", filename);
        free(entries);
        return;
    }
    
    // Get the file's first cluster
    unsigned int file_cluster = (file_entry->fstclushi << 16) | file_entry->fstcluslo;
    
    // Free all clusters in the chain (if file has any clusters)
    if (file_cluster != 0 && file_cluster < 0x0FFFFFF8) {
        unsigned int cluster = file_cluster;
        while (cluster != 0 && cluster < 0x0FFFFFF8) {
            // Get next cluster before freeing current one
            unsigned int next_cluster = get_next_cluster(img, b, cluster);
            
            // Mark current cluster as free (write 0 to FAT entry)
            unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + cluster * 4;
            unsigned int free_marker = 0x00000000;
            fseek(img, fat_offset, SEEK_SET);
            fwrite(&free_marker, sizeof(unsigned int), 1, img);
            fflush(img);
            
            cluster = next_cluster;
        }
    }
    
    // Mark directory entry as deleted (0xE5)
    unsigned int cluster_size = b->BytesPerSec * b->SecPerClus;
    int entries_per_cluster = cluster_size / sizeof(dir_entry);
    
    unsigned int dir_cluster = current_cluster;
    int target_entry = entry_index;
    while (target_entry >= entries_per_cluster) {
        dir_cluster = get_next_cluster(img, b, dir_cluster);
        target_entry -= entries_per_cluster;
    }
    
    unsigned int entry_offset = get_cluster_offset(b, dir_cluster) + (target_entry * sizeof(dir_entry));
    unsigned char deleted_marker = 0xE5;
    fseek(img, entry_offset, SEEK_SET);
    fwrite(&deleted_marker, 1, 1, img);
    fflush(img);
    
    free(entries);
}

void rmdir_cmd(char* dirname, FILE* img, BPB* b, unsigned int current_cluster, file_table* table) {
    
    // Find the directory entry
    int entry_count = 0;
    dir_entry* entries = read_dir_chain(img, b, current_cluster, &entry_count);
    if (entries == NULL) {
        printf("Error: could not read directory\n");
        return;
    }
    
    dir_entry* dir_entry_ptr = NULL;
    int entry_index = -1;
    for (int i = 0; i < entry_count; i++) {
        if (is_longname(&entries[i])) continue;
        char* trimmed = trim_filename((char*)entries[i].name, 11);
        if (strcmp(trimmed, dirname) == 0) {
            dir_entry_ptr = &entries[i];
            entry_index = i;
            free(trimmed);
            break;
        }
        free(trimmed);
    }
    
    if (dir_entry_ptr == NULL) {
        printf("Error: %s does not exist\n", dirname);
        free(entries);
        return;
    }
    
    // Check if it's a directory
    if (!is_directory(dir_entry_ptr)) {
        printf("Error: %s is not a directory\n", dirname);
        free(entries);
        return;
    }
    
    // Get the directory's first cluster
    unsigned int dir_cluster = (dir_entry_ptr->fstclushi << 16) | dir_entry_ptr->fstcluslo;
    
    // Check if directory is empty (only "." and ".." allowed)
    int dir_entry_count = 0;
    dir_entry* dir_entries = read_dir_chain(img, b, dir_cluster, &dir_entry_count);
    if (dir_entries == NULL) {
        printf("Error: could not read directory contents\n");
        free(entries);
        return;
    }
    
    // Check for any files open in the directory
    for (int i = 0; i < 10; i++) {
        if (table[i].isopen == 1) {
            // Check if path contains this directory
            // This is a simple check - you might need more sophisticated path matching
            for (int j = 0; j < dir_entry_count; j++) {
                if (is_longname(&dir_entries[j])) continue;
                char* trimmed = trim_filename((char*)dir_entries[j].name, 11);
                if (strcmp(table[i].filename, trimmed) == 0) {
                    printf("Error: a file is open in directory %s\n", dirname);
                    free(trimmed);
                    free(dir_entries);
                    free(entries);
                    return;
                }
                free(trimmed);
            }
        }
    }
    
    // Count non-. and non-.. entries
    int real_entries = 0;
    for (int i = 0; i < dir_entry_count; i++) {
        if (is_longname(&dir_entries[i])) continue;
        
        char* trimmed = trim_filename((char*)dir_entries[i].name, 11);
        
        // Skip "." and ".."
        if (strcmp(trimmed, ".") != 0 && strcmp(trimmed, "..") != 0) {
            real_entries++;
        }
        free(trimmed);
    }
    
    free(dir_entries);
    
    if (real_entries > 0) {
        printf("Error: directory %s is not empty\n", dirname);
        free(entries);
        return;
    }
    
    // Free all clusters used by the directory
    unsigned int cluster = dir_cluster;
    while (cluster != 0 && cluster < 0x0FFFFFF8) {
        // Get next cluster before freeing current one
        unsigned int next_cluster = get_next_cluster(img, b, cluster);
        
        // Mark current cluster as free (write 0 to FAT entry)
        unsigned int fat_offset = b->RsvdSecCnt * b->BytesPerSec + cluster * 4;
        unsigned int free_marker = 0x00000000;
        fseek(img, fat_offset, SEEK_SET);
        fwrite(&free_marker, sizeof(unsigned int), 1, img);
        fflush(img);
        
        cluster = next_cluster;
    }
    
    // Mark directory entry as deleted (0xE5)
    unsigned int cluster_size = b->BytesPerSec * b->SecPerClus;
    int entries_per_cluster = cluster_size / sizeof(dir_entry);
    
    unsigned int parent_cluster = current_cluster;
    int target_entry = entry_index;
    while (target_entry >= entries_per_cluster) {
        parent_cluster = get_next_cluster(img, b, parent_cluster);
        target_entry -= entries_per_cluster;
    }
    
    unsigned int entry_offset = get_cluster_offset(b, parent_cluster) + (target_entry * sizeof(dir_entry));
    unsigned char deleted_marker = 0xE5;
    fseek(img, entry_offset, SEEK_SET);
    fwrite(&deleted_marker, 1, 1, img);
    fflush(img);
    
    free(entries);
}
