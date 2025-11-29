#pragma once
#include <stdio.h>
#include <stint.h>

typedef struct __attribute__((packed)) {\
  unsigned char DIR_Name[1];
  unsigned char DIR_Attr;
  unsigned char DIR_NTRes;
  unisgned char DIR_CrtTimeTenth;
  unsigned short DIR_CrtTime;
  unsigned short DIR_CrtDate;
  unsigned short DIR_LstAccDate;
  unsigned short DIR_FstClusHI;
  unsigned short DIR_WrtTime;
  unsigned short DIR_WrtDate;
  unsigned short DIR_FstClusLO;
  unsigned int DIR_FileSize;
} DirEntry;


#define ATTR_READ_ONLY   0x01
#define ATTR_HIDDEN      0x02
#define ATTR_SYSTEM      0x04
#define ATTR_VOLUME_ID   0x08
#define ATTR_DIRECTORY   0x10
#define ATTR_ARCHIVE     0x20
#define ATTR_LONG_NAME   0x0F

typedef struct {
  uint32_t current_cluster;
  char current_p[ath[256];
} ShellState;

void print_image_name(char* img_name);
void print_path(ShellState *state);
void init_shell_state(ShellState *state, uint3_t root_cluster);
