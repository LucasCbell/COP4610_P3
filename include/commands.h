#pragma once

#include <stdio.h>
#include "file_ops.h"

// Part 1: Info
void info(BPB *b);

// Part 2: Navigation
void ls(FILE *img, BPB *b, unsigned int cluster);
unsigned int cd(FILE *img, BPB *b, unsigned int current_cluster, char *dirname);

// part 3: creat and mkdir
void fat32_mkdir(FILE *img, BPB *b, unsigned int current_cluster, const char *dirname);
void fat32_creat(FILE *img, BPB *b, unsigned int current_cluster, const char *filename);

// Part 4: Read 
void open(char* filename, char* flags, FILE* img, BPB* b, unsigned int current_cluster, file_table* table, char* img_name);
void close(char* filename, file_table* table);
void lsof(file_table* table);
void lseek(char* filename, int offset, file_table* table);
void read(char* filename, int size, FILE* img, BPB* b, file_table* table, unsigned int current_cluster);

// part 5: mv and write
void write_file(char* filename, char* string, FILE* img, BPB* b, file_table* table, unsigned int current_cluster);
void mv(char* src, char* dest, FILE* img, BPB* b, unsigned int current_cluster, file_table* table);

// part 6: rm and rmdir
void rm(char* filename, FILE* img, BPB* b, unsigned int current_cluster, file_table* table);
void rmdir_cmd(char* dirname, FILE* img, BPB* b, unsigned int current_cluster, file_table* table);
