#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "meta.h"
#include "rm.h"
#include "util.h"

/*
 * Remove every vmd-* project directory and report how many were removed.
 */
static int remove_all_projects(const char *home, size_t *removed){
    char projects_path[PATH_MAX];
    DIR *projects;
    struct dirent *entry;
    struct stat info;
    int result = 0;

    *removed = 0;
    if(build_path(projects_path, sizeof(projects_path), "%s/vmd/projects", home) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(lstat(projects_path, &info) != 0){
        if(errno == ENOENT){
            return 0;
        }
        perror(projects_path);
        return 1;
    }

    projects = opendir(projects_path);
    if(projects == NULL){
        perror(projects_path);
        return 1;
    }

    while((entry = readdir(projects)) != NULL){
        char project_path[PATH_MAX];
        struct stat info;

        if(strncmp(entry->d_name, "vmd-", 4) != 0 || valid_name(entry->d_name + 4) == 0){
            continue;
        }
        if(build_path(project_path, sizeof(project_path), "%s/%s",
                      projects_path, entry->d_name) != 0){
            continue;
        }
        if(lstat(project_path, &info) != 0){
            perror(project_path);
            result = 1;
            break;
        }
        if(S_ISDIR(info.st_mode) == 0 && S_ISLNK(info.st_mode) == 0){
            continue;
        }
        if(remove_tree(project_path) != 0){
            result = 1;
            break;
        }
        (*removed)++;
    }

    closedir(projects);
    return result;
}

/*
 * Run "vmd rm": remove one version, one project, or every project and update the index.
 */
int rm(int argc, char **argv){
    const char *home = vmd_home();
    char project[PATH_MAX];
    char version_path[PATH_MAX];
    struct stat info;
    const char *name;
    int version = 0;
    int all_versions = 0;

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(argc == 3 && strcmp(argv[2], "all") == 0){
        size_t removed = 0;
        if(remove_all_projects(home, &removed) != 0){
            fprintf(stderr, "error: could not remove every project\n");
            return 1;
        }
        if(meta_clear() != 0){
            fprintf(stderr, "warning: projects removed, but the index was not cleared\n");
        }
        printf("removed %zu project%s\n", removed, removed == 1 ? "" : "s");
        return 0;
    }

    name = argv[2];
    if(valid_name(name) == 0){
        fprintf(stderr, "error: invalid project name\n");
        fprintf(stderr, "use letters, numbers, '-' or '_' (1-%d characters); \"all\" is reserved\n",
                VMD_NAME_MAX - 1);
        return 1;
    }
    if(argc == 4){
        if(strcmp(argv[3], "all") == 0){
            all_versions = 1;
        }else{
            version = parse_version(argv[3]);
            if(version <= 0){
                fprintf(stderr, "error: the version must be a positive number or \"all\"\n");
                return 1;
            }
        }
    }
    if(build_path(project, sizeof(project), "%s/vmd/projects/vmd-%s", home, name) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }

    if(argc == 3 || all_versions){
        if(lstat(project, &info) != 0){
            fprintf(stderr, "error: project \"%s\" has not been added\n", name);
            return 1;
        }
        if(remove_tree(project) != 0){
            fprintf(stderr, "error: could not remove \"%s\"\n", name);
            return 1;
        }
        if(meta_forget(name, 0) != 0){
            fprintf(stderr, "warning: project removed, but the index was not updated\n");
        }
        printf("removed project \"%s\" (all versions)\n", name);
        return 0;
    }

    if(build_path(version_path, sizeof(version_path), "%s/v%d", project, version) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(lstat(version_path, &info) != 0 || S_ISDIR(info.st_mode) == 0){
        fprintf(stderr, "error: version v%d of \"%s\" has not been added\n", version, name);
        return 1;
    }
    if(remove_tree(version_path) != 0){
        fprintf(stderr, "error: could not remove \"%s\" v%d\n", name, version);
        return 1;
    }
    if(meta_forget(name, version) != 0){
        fprintf(stderr, "warning: version removed, but the index was not updated\n");
    }
    printf("removed \"%s\" v%d\n", name, version);

    if(next_version(project) == 1){
        if(remove_tree(project) == 0){
            if(meta_forget(name, 0) != 0){
                fprintf(stderr, "warning: project folder removed, but the index was not updated\n");
            }
            printf("project \"%s\" had no versions left, so its folder was removed\n", name);
        }
    }
    return 0;
}
