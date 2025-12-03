void info(BPB * b);
void ls(FILE* img, BPB* b, unsigned int cluster);
unsigned int cd(FILE* img, BPB* b, unsigned int current_cluster, char* dirname);
void open(char* filename, char* flags, FILE* img, BPB* b, unsigned int current_cluster, file_table* table);
void close(char* filename, file_table* table);
void lsof(file_table* table);
void lseek(char* filename, int offset, file_table* table);
void read(char* filename, int size, FILE* img, BPB* b, file_table* table, unsigned int current_cluster);