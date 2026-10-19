#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "up.h"

void pup(char *name, char *version){
    char cmd[200];
    snprintf(cmd, sizeof(cmd), "mkdir -p ~/vmd/projects/vmd-%s/v%s", name, version);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r . ~/vmd/projects/vmd-%s/v%s", name, version);
    system(cmd);
}

int up(char **argv){

    char name[30];

    if(strlen(argv[2]) >= 30){
        printf("what are you doing my friend this's name or text\n");
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
        snprintf(cmd, sizeof(cmd), "ls -v %s/vmd/projects/vmd-%s | tail -n 1", getenv("HOME"), name);

        FILE *fp = popen(cmd, "r");
        if(fp == NULL){
            printf("error in command\n");
            return 1;
        }
        char lastV[30] = "";
        fgets(lastV, sizeof(lastV), fp);
        pclose(fp);
        lastV[strcspn(lastV, "\n")] = 0;

        int n = 1;
        
        if(lastV[0] == 'v' || lastV[0] == 'V'){
            n = atoi(&lastV[1]);
            if(n <= 0) n = 1; 
        }

        char version[30];
        snprintf(version, sizeof(version), "%d", n + 1);

        pup(name, version);

    } else {
        printf("the project has not been added\n");
        printf("use vmd help\n");
        return 1;
    }
    return 0;
}