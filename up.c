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
 * Save the current folder as the next version of a project.
 *
 * The next number is the highest saved version plus one. As in add, the
 * copy happens in a temporary folder that is renamed to "vN" at the end.
 */
int up(const char *name, const char *message){
    const char *home = vmd_home();
    char cwd[PATH_MAX];
    char cwd_real[PATH_MAX];
    char project[PATH_MAX];
    char project_real[PATH_MAX];
    char temporary[PATH_MAX];
    char version[PATH_MAX];
    struct stat info;
    vmd_stats stats = {0};
    int next;
    char size[32];

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(valid_name(name) == 0){
        fprintf(stderr, "error: invalid project name\n");
        fprintf(stderr, "use letters, numbers, '-' or '_' (1-%d characters); \"all\" is reserved\n",
                VMD_NAME_MAX - 1);
        return 1;
    }
    if(getcwd(cwd, sizeof(cwd)) == NULL){
        perror("getcwd");
        return 1;
    }
    if(build_path(project, sizeof(project), "%s/vmd/projects/vmd-%s", home, name) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(lstat(project, &info) != 0){
        fprintf(stderr, "error: project \"%s\" has not been added\n", name);
        fprintf(stderr, "use \"vmd add %s\" first\n", name);
        return 1;
    }
    if(S_ISDIR(info.st_mode) == 0){
        fprintf(stderr, "error: %s is not a project directory\n", project);
        return 1;
    }

    next = next_version(project);
    if(next <= 0){
        perror(project);
        return 1;
    }
    if(build_path(temporary, sizeof(temporary), "%s/.v%d.tmp.XXXXXX", project, next) != 0 ||
       build_path(version, sizeof(version), "%s/v%d", project, next) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(realpath(cwd, cwd_real) == NULL || realpath(project, project_real) == NULL){
        perror("realpath");
        return 1;
    }
    if(is_inside(project_real, cwd_real)){
        fprintf(stderr, "error: the storage folder is inside the folder you are saving\n");
        fprintf(stderr, "run \"vmd up\" from a subfolder, not from %s\n", cwd_real);
        return 1;
    }

    if(mkdtemp(temporary) == NULL){
        perror(temporary);
        return 1;
    }
    if(copy_project(cwd, temporary, &stats) != 0){
        fprintf(stderr, "error: could not copy the project files\n");
        remove_tree(temporary);
        return 1;
    }
    if(rename(temporary, version) != 0){
        if(errno == EEXIST || errno == ENOTEMPTY){
            fprintf(stderr, "error: v%d already exists\n", next);
        }else{
            perror(version);
        }
        remove_tree(temporary);
        return 1;
    }

    if(meta_record(name, next, message, &stats) != 0){
        fprintf(stderr, "warning: v%d was saved, but the index was not updated\n", next);
        fprintf(stderr, "run \"vmd info --rebuild\" to fix it\n");
    }

    human_size(stats.bytes, size, sizeof(size));
    if(next == 1){
        printf("added \"%s\" as v1 (%lu files, %s)\n", name, stats.files, size);
    }else if(stats.skipped > 0){
        printf("updated \"%s\" to v%d (%lu files, %s, %lu skipped)\n",
               name, next, stats.files, size, stats.skipped);
    }else{
        printf("updated \"%s\" to v%d (%lu files, %s)\n", name, next, stats.files, size);
    }
    return 0;
}
