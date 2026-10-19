#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "imp.h"

void import(char *name, char *version, char *src, char *pathimp){

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s/vmd-%s_v-%s\"", pathimp, name, version);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r \"%s/.\" \"%s/vmd-%s_v-%s\"", src, pathimp, name, version);
    system(cmd);
}

int imp(char **argv){

    char name[30];
    char version[30];
    char pathimp[200];

    if(strlen(argv[2]) >= 30){
        printf("what are you doing my frind this's name or text\n");
        return 1;
    }
    strcpy(name, argv[2]);
    if(strlen(argv[3]) >= 30){
        printf("what are you doing my frind this's name or text\n");
        return 1;
    }
    strcpy(version, argv[3]);
    if(strlen(argv[4]) >= 200){
        printf("what are you doing my frind this's name or text\n");
        return 1;
    }
    strcpy(pathimp, argv[4]);

        for(int i = 0; name[i] != '\0'; i++){
        char c = name[i];
        if(isalnum(c) == 0 && c != '-' && c != '_'){
            printf("in your project name use only characters , numbers and '-','_'\n");
            return 1;
        }
    }
    for(int i = 0; version[i] != '\0'; i++){
        if(isdigit(version[i]) == 0){
            printf("the version must be a number\n");
            return 1;
        }
    }

        char path[200];
        sprintf(path, "%s/vmd/projects/vmd-%s", getenv("HOME"), name);
        struct stat info;
    if(stat(path, &info) != 0){

        printf("the project not has been added\n");
        printf("use vmd help\n");
    return 1;
    }
    sprintf(path, "%s/vmd/projects/vmd-%s/v%s", getenv("HOME"), name, version);
    if(stat(path, &info) != 0){
     
        printf("the version not has been added\n");
        printf("use vmd help\n");
     return 1;
    }
    import(name, version, path, pathimp);

return 0;
}