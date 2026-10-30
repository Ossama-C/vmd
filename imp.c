#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vmd.h"

/*
 * Export one saved version into <destination>/vmd-<project>_v-<version>.
 *
 * The copy goes to a temporary folder next to the target and is renamed at
 * the end. An older export with the same name is replaced as a whole.
 */
int imp(const char *name, int version, const char *destination){
    const char *home = vmd_home();
    char source[PATH_MAX];
    char source_real[PATH_MAX];
    char destination_real[PATH_MAX];
    char target[PATH_MAX];
    char temporary[PATH_MAX];
    struct stat info;
    vmd_stats stats = {0};
    int replacing = 0;
    char size[32];

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(valid_name(name) == 0){
        fprintf(stderr, "error: invalid project name\n");
        return 1;
    }
    if(destination == NULL || destination[0] == '\0'){
        fprintf(stderr, "error: the destination path is empty\n");
        return 1;
    }
    if(build_path(source, sizeof(source), "%s/vmd/projects/vmd-%s/v%d",
                  home, name, version) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(lstat(source, &info) != 0 || S_ISDIR(info.st_mode) == 0){
        fprintf(stderr, "error: \"%s\" v%d has not been added\n", name, version);
        return 1;
    }
    if(realpath(source, source_real) == NULL){
        perror("realpath");
        return 1;
    }

    if(mkdir_p(destination, 0755) != 0){
        perror(destination);
        return 1;
    }
    if(realpath(destination, destination_real) == NULL){
        perror(destination);
        return 1;
    }
    if(is_inside(destination_real, source_real)){
        fprintf(stderr, "error: you cannot export a version into itself\n");
        return 1;
    }
    if(build_path(target, sizeof(target), "%s%svmd-%s_v-%d",
                  destination_real, strcmp(destination_real, "/") == 0 ? "" : "/",
                  name, version) != 0){
        fprintf(stderr, "error: the destination path is too long\n");
        return 1;
    }
    if(is_inside(target, source_real)){
        fprintf(stderr, "error: you cannot export a version into itself\n");
        return 1;
    }
    if(build_path(temporary, sizeof(temporary), "%s.tmp.XXXXXX", target) != 0){
        fprintf(stderr, "error: the destination path is too long\n");
        return 1;
    }

    if(lstat(target, &info) == 0){
        if(S_ISLNK(info.st_mode) || S_ISDIR(info.st_mode) == 0){
            fprintf(stderr, "error: %s exists and is not a directory\n", target);
            return 1;
        }
        replacing = 1;
    }else if(errno != ENOENT){
        perror(target);
        return 1;
    }

    if(mkdtemp(temporary) == NULL){
        perror(temporary);
        return 1;
    }
    if(copy_tree(source, temporary, &stats) != 0){
        fprintf(stderr, "error: could not export \"%s\" v%d\n", name, version);
        remove_tree(temporary);
        return 1;
    }
    if(replacing && remove_tree(target) != 0){
        fprintf(stderr, "error: could not replace the old export %s\n", target);
        remove_tree(temporary);
        return 1;
    }
    if(rename(temporary, target) != 0){
        perror(target);
        remove_tree(temporary);
        return 1;
    }

    human_size(stats.bytes, size, sizeof(size));
    printf("exported \"%s\" v%d to %s (%lu files, %s)%s\n",
           name, version, target, stats.files, size,
           replacing ? ", replaced the old export" : "");
    return 0;
}
