#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "add.h"

int add(char **argv){

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

    char cmd[200];
    char path[200];
    snprintf(path, sizeof(path), "%s/vmd/projects/vmd-%s", getenv("HOME"), name);
    struct stat info;
    
    if(stat(path, &info) == 0){
        printf("the project has been added\n");
        printf("use vmd help\n");
        return 1;
     } else {
        snprintf(cmd, sizeof(cmd), "mkdir -p ~/vmd/projects/vmd-%s/v1", argv[2]);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "cp -r . ~/vmd/projects/vmd-%s/v1", argv[2]);
        system(cmd);
    }
    return 0;
}