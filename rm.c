#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ftw.h>
#include <dirent.h>
#include "rm.h"

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *buffer){
    (void)info;
    (void)type;
    (void)buffer;
    return remove(path);
}

static int remove_project_tree(const char *path){
    return nftw(path, remove_entry, 64, FTW_DEPTH | FTW_PHYS);
}

static int remove_all_projects(const char *home){
    char projects_path[200];
    char project_path[200];
    struct stat info;
    int length = snprintf(projects_path, sizeof(projects_path), "%s/vmd/projects", home);

    if(length < 0 || (size_t)length >= sizeof(projects_path)){
        printf("project path is too long\n");
        return 1;
    }
    if(lstat(projects_path, &info) != 0){
        printf("no project has been added yet\n");
        printf("use vmd help\n");
        return 0;
    }
    if(S_ISDIR(info.st_mode) == 0){
        printf("project path is not a directory\n");
        return 1;
    }

    DIR *projects = opendir(projects_path);
    if(projects == NULL){
        perror("failed to open projects");
        return 1;
    }

    struct dirent *entry;
    int result = 0;
    while((entry = readdir(projects)) != NULL){
        if(strncmp(entry->d_name, "vmd-", 4) != 0){
            continue;
        }

        length = snprintf(project_path, sizeof(project_path), "%s/%s", projects_path, entry->d_name);
        if(length < 0 || (size_t)length >= sizeof(project_path)){
            printf("project path is too long\n");
            result = 1;
            break;
        }
        if(lstat(project_path, &info) != 0){
            perror("failed to find project");
            result = 1;
            break;
        }
        if(S_ISDIR(info.st_mode) == 0 && S_ISLNK(info.st_mode) == 0){
            continue;
        }
        if(remove_project_tree(project_path) != 0){
            perror("failed to remove project");
            result = 1;
            break;
        }
    }

    closedir(projects);
    return result;
}

int rm(int argc, char **argv){

    char path[200];
    struct stat info;
    char *home = getenv("HOME");

    if(argc != 3 && argc != 4){
        printf("invalede command us \nvmd help\n");
        return 1;
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

    if(home == NULL || home[0] == '\0'){
        printf("could not find the home directory\n");
        return 1;
    }

    if(argc == 3 && strcmp(name, "all") == 0){
        return remove_all_projects(home);
    }

    int length;
    int remove_all_versions = argc == 4 && strcmp(argv[3], "all") == 0;
    if(argc == 4 && remove_all_versions == 0){
        char version[30];
        if(strlen(argv[3]) == 0 || strlen(argv[3]) >= sizeof(version)){
            printf("the version must be a number\n");
            return 1;
        }
        strcpy(version, argv[3]);
        for(int i = 0; version[i] != '\0'; i++){
            if(isdigit((unsigned char)version[i]) == 0){
                printf("the version must be a number\n");
                return 1;
            }
        }
        length = snprintf(path, sizeof(path), "%s/vmd/projects/vmd-%s/v%s", home, name, version);
    }else{
        length = snprintf(path, sizeof(path), "%s/vmd/projects/vmd-%s", home, name);
    }

    if(length < 0 || (size_t)length >= sizeof(path)){
        printf("project path is too long\n");
        return 1;
    }

    if(stat(path, &info) != 0){
        if(argc == 4 && remove_all_versions == 0){
            printf("the version has not been added\n");
        }else{
            printf("the project has not been added\n");
        }
        printf("use vmd help\n");
        return 1;
    }

    if(argc == 4 && remove_all_versions == 0 && S_ISDIR(info.st_mode) == 0){
        printf("the version has not been added\n");
        printf("use vmd help\n");
        return 1;
    }

    if(remove_project_tree(path) != 0){
        perror("failed to remove project");
        return 1;
    }

    return 0;
}