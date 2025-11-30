#include "common.h"

#define MAX_PATH_LEN 256
char current_path[MAX_PATH_LEN] = "/";

void print_image_name(char* img_name) {
    printf("%s", img_name);
}

void print_path(char* current_path) {
    printf("%s", current_path);
}

void init_path(void) {
    strcpy(current_path, "/");
}

void update_path(char* dirname, int is_entering) {
    if (is_entering) {
        // entering a dir - append to path
        if (strcmp(dirname, "..") == 0) {
            // go back to parent dir
            int len = strlen(current_path);
            if (len > 1) {
                // remove trailing slash if exists
                if (current_path[len - 1] == '/') {
                    current_path[len - 1] = '\0';
                    len--;
                }
                // find last slash
                int last_slash = len - 1;
                while (last_slash > 0 && current_path[last_slash] != '/') {
                    last_slash--;
                }
                // truncate at last slash
                current_path[last_slash + 1] = '\0';
            }
        } else if (strcmp(dirname, ".") != 0) {
            // enter a subdirectory (skip ".")
            if (strlen(current_path) + strlen(dirname) + 2 < MAX_PATH_LEN) {
                if (current_path[strlen(current_path) - 1] != '/') {
                    strcat(current_path, "/");
                }
                strcat(current_path, dirname);
                strcat(current_path, "/");
            }
        }
    }
}