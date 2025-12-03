#include "common.h"
file_table table[10];

int main(int argc, char* argv[]) {

    FILE *img;      // file ptr to img file
    bool exit = 0;
    unsigned int current_cluster;  // track current working dir cluster

    BPB *bpb = malloc(sizeof(BPB));

    //initialzing the file table empty
    for (int i = 0; i < 10; i++) {
        table[i].isopen = 0;
        table[i].index = i;
        memset(table[i].filename, 0, 256);
        memset(table[i].path, 0, 512);
    }

    if(bpb == NULL) {
        printf("Memory allocation failed.");
        return -1;
    }

    if(argc == 2) {
        printf("%s\n", argv[0]);  // executable name  (./filesys)
        printf("%s\n", argv[1]);  // "first" argument (the file we want to mount)
    } else {
        printf("Incorrect Arguments\n");
    }

    /* to "MOUNT" the file
            1. Open the file (big array of bytes)
            2. Read the boot sector
                2.1 Read boot sector (the first 512 bytes of img file)
                2.2 Gather information about the img file
                    - number of bytes per sector
                    - number of sectors per cluster
                    - reserved sectors
                    - how many FATs
                    - size of each FAT
                    - cluster number of root directory
            3. Compute the offsets to FAT and Data Region
                3.1 Using information in boot sector, compute:
                    - Reserved region
                    - FAT region
                    - Data region
                3.2 Spefically, compute the byte location of:
                    - start of FAT1
                    - start of FAT2
                    - start of data region
                    - formula for finding cluster N's offset
            4. Store the filesys metadata in a struct
                 Consists of:
                    - file descriptor
                    - bytes per sector
                    - sectors per cluster
                    - reserved sectors
                    - number of FATs
                    - root directory cluster
                    - computed byte offset of FAT1
                    - computed byte offset of FAT2
                    - computed byte offset of start of data region
                    - cluster of current working directory
    */

    // attempt to open the .img file
    // if cant open, return error
    // r+ = do reads and writes on the file
    img = fopen(argv[1], "r+");

    if (img == NULL) {
        // error, exit the program
        printf("ERROR: The file %s does not exist.\n", argv[1]);
        return 1;
    } else {
        printf("SUCCESS: The file %s was opened.\n", argv[1]);
    }
    
    // read the boot sector
    unsigned char boot_sector[512];
    read_boot_sector(img, boot_sector);

    // get information from the boot_sector
    parse_boot_sector(bpb, boot_sector);

    // initialize current dir to root cluster
    current_cluster = bpb->RootClus;
    
    // initialize path tracking
    init_path();

    // main shell loop
    while (exit == 0) {
        // print image name "fat32.img"
        print_image_name(argv[1]);

        // print path in image
        // "/" for root
        // "/FOLDER1/FOLDER2/"
        extern char current_path[256];
        print_path(current_path);

		printf("> ");

		/* input contains the whole command
		 * tokens contains substrings from input split by spaces */

		char *input = get_input();

		tokenlist *tokens = get_tokens(input);

        // exit command
        if((strcmp(tokens->items[0], "exit") == 0) 
                    && tokens->size == 1) {
            exit = 1;
        }

        // info command
        if ((strcmp(tokens->items[0], "info") == 0) && tokens->size == 1) {
            info(bpb);
        }

        // ls command
        if ((strcmp(tokens->items[0], "ls") == 0) && tokens->size == 1) {
            ls(img, bpb, current_cluster);
        }

        // cd command
        if ((strcmp(tokens->items[0], "cd") == 0) && tokens->size == 2) {
            unsigned int new_cluster = cd(img, bpb, current_cluster, tokens->items[1]);
            if (new_cluster != 0) {
                current_cluster = new_cluster;
                update_path(tokens->items[1], 1);
            }
        }

        // open command
        if ((strcmp(tokens->items[0], "open") == 0) && tokens->size == 3) {
            open(tokens->items[1], tokens->items[2], img, bpb, current_cluster, table);
        }
        if ((strcmp(tokens->items[0], "close") == 0) && tokens->size == 2) {
            close(tokens->items[1], table);
        }
        
        if ((strcmp(tokens->items[0], "lsof") == 0) && tokens->size == 1) {
            lsof(table);
        }
        
        if ((strcmp(tokens->items[0], "lseek") == 0) && tokens->size == 3) {
            int offset = atoi(tokens->items[2]);
            lseek(tokens->items[1], offset, table);
        }
        /*
        if ((strcmp(tokens->items[0], "read") == 0) && tokens->size == 3) {
            read(tokens->items[1], size, img, bpb, table);
        }
        */

		free(input);
		free_tokens(tokens);
	}

    // close img file
    fclose(img);

    // free remaining memory
    free(bpb);

    return 0;
}
