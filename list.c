#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "list.h"

int list(int argc, char **argv){

    char path[200];
    char cmd[300];
    struct stat info;

    if(argc == 2){

        snprintf(path, sizeof(path), "%s/vmd/projects", getenv("HOME"));
        if(stat(path, &info) != 0){
            printf("no project has been added yet\n");
            printf("use vmd help\n");
            return 0;
        }

        snprintf(cmd, sizeof(cmd), "ls \"%s\" | sed 's/^vmd-//'", path);
        return system(cmd) != 0;
    }

    char name[30];

    if(strlen(argv[2]) >= 30){
        printf("what are you doing my friend this name or text\n");
        return 1;
    }
    strcpy(name, argv[2]);

    for(int i = 0; name[i] != '\0'; i++){
        char c = name[i];
        if(isalnum(c) == 0 && c != '-' && c != '_'){
            printf("in your project name use only characters , numbers and '-','_'\n");
            return 1;
        }
    }

    snprintf(path, sizeof(path), "%s/vmd/projects/vmd-%s", getenv("HOME"), name);
    if(stat(path, &info) != 0){
        printf("the project has not been added\n");
        printf("use vmd help\n");
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "ls -v \"%s\"", path);
    return system(cmd) != 0;
}