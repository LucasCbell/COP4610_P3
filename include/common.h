#pragma once

#include "lexer.h"
#include "shell.h"
#include "file_ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// structs:
typedef struct __attribute__((packed)){
    // need to change to FAT32 specs
    unsigned char BS_jmpBoot[3];
    unsigned char BS_OEMName[8];
    unsigned short BPB_BytesPerSec;
    unsigned char BPB_SecPerClus;
    unsigned short BPB_RsvdSecCnt;
} BPB;
