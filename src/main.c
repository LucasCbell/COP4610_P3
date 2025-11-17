#include "common.h"

int main(int argc, char* argv[]) {

    FILE *img;

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

    // debug
    printf("Boot Sector:");
    for(int i = 0; i < 512; i++) {
        if(i % 16 == 0)
            printf("\n\t");
        // %X: print hex. %2: print two chars, %0 zeros for padding
        printf("%02X ", boot_sector[i]);
        fflush(stdout);
    }
    printf("\n");

    while (1) {
        // print image name "fat32.img"
        print_image_name(argv[1]);

        // print path in image
        // "/" for root
        // "/FOLDER1/FOLDER2/"
        print_path();

		printf("> ");

		/* input contains the whole command
		 * tokens contains substrings from input split by spaces */

		char *input = get_input();
		printf("whole input: %s\n", input);

		tokenlist *tokens = get_tokens(input);
		for (int i = 0; i < tokens->size; i++) {
			printf("token %d: (%s)\n", i, tokens->items[i]);
		}

		free(input);
		free_tokens(tokens);
	}

    // close img file
    fclose(img);

    return 0;
}
