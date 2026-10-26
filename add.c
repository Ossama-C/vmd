#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "add.h"
#include "meta.h"
#include "util.h"

/*
 * Save the current working directory as version 1 of a project.
 * Creates the storage folders, copies the tree into a temporary folder,
 * renames it into place, then records the version in the index.
 */
int add(const char *name, const char *message){
    const char *home = vmd_home();
    char cwd[PATH_MAX];
    char cwd_real[PATH_MAX];
    char projects[PATH_MAX];
    char project[PATH_MAX];
    char project_real[PATH_MAX];
    char temporary[PATH_MAX];
    char version[PATH_MAX];
    struct stat info;
    copy_stats stats = {0};
    int made_project = 0;

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
    if(build_path(projects, sizeof(projects), "%s/vmd/projects", home) != 0 ||
       build_path(project, sizeof(project), "%s/vmd-%s", projects, name) != 0 ||
       build_path(temporary, sizeof(temporary), "%s/.v1.tmp.XXXXXX", project) != 0 ||
       build_path(version, sizeof(version), "%s/v1", project) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }

    if(lstat(project, &info) == 0){
        if(S_ISDIR(info.st_mode) == 0){
            fprintf(stderr, "error: %s exists and is not a directory\n", project);
            return 1;
        }
        if(next_version(project) > 1){
            fprintf(stderr, "error: project \"%s\" already exists\n", name);
            fprintf(stderr, "use \"vmd up %s\" to save a new version\n", name);
            return 1;
        }
    }else if(errno != ENOENT){
        perror(project);
        return 1;
    }else{
        if(mkdir_p(projects, 0755) != 0){
            perror(projects);
            return 1;
        }
        if(mkdir(project, 0755) != 0){
            perror(project);
            return 1;
        }
        made_project = 1;
    }

    if(realpath(cwd, cwd_real) == NULL || realpath(project, project_real) == NULL){
        perror("realpath");
        if(made_project){
            rmdir(project);
        }
        return 1;
    }
    if(is_inside(project_real, cwd_real)){
        fprintf(stderr, "error: the destination is inside the folder you are saving\n");
        fprintf(stderr, "run \"vmd add\" from a subfolder of your project, not from %s\n", cwd_real);
        if(made_project){
            rmdir(project);
        }
        return 1;
    }

    if(mkdtemp(temporary) == NULL){
        perror(temporary);
        if(made_project){
            rmdir(project);
        }
        return 1;
    }
    if(copy_project(cwd, temporary, &stats) != 0){
        fprintf(stderr, "error: could not copy the project files\n");
        remove_tree(temporary);
        if(made_project){
            rmdir(project);
        }
        return 1;
    }
    if(rename(temporary, version) != 0){
        perror(version);
        remove_tree(temporary);
        if(made_project){
            rmdir(project);
        }
        return 1;
    }

    if(meta_record(name, 1, message, &stats) != 0){
        fprintf(stderr, "warning: version saved, but the index was not updated\n");
        fprintf(stderr, "run \"vmd info --rebuild\" to fix it\n");
    }

    char size[32];
    human_size(stats.bytes, size, sizeof(size));
    if(stats.skipped > 0){
        printf("added \"%s\" as v1 (%lu files, %s, %lu skipped by .vmdignore)\n",
               name, stats.files, size, stats.skipped);
    }else{
        printf("added \"%s\" as v1 (%lu files, %s)\n", name, stats.files, size);
    }
    return 0;
}
