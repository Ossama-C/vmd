#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vmd.h"

/*
 * Remove every vmd-* project folder and count what was removed.
 *
 * Each project is first renamed to a hidden name in the same directory, so
 * it disappears from the store atomically even if deleting its files fails.
 * A failure on one project does not stop the others; the failure is
 * remembered and returned at the end.
 */
static int remove_all_projects(const char *home, int *removed){
    char projects_path[PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    struct stat info;
    int result = 0;

    *removed = 0;
    if(build_path(projects_path, sizeof(projects_path), "%s/vmd/projects", home) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(lstat(projects_path, &info) != 0){
        return errno == ENOENT ? 0 : 1;
    }
    dir = opendir(projects_path);
    if(dir == NULL){
        perror(projects_path);
        return 1;
    }
    while((entry = readdir(dir)) != NULL){
        char path[PATH_MAX];
        char hidden[PATH_MAX];

        if(strncmp(entry->d_name, "vmd-", 4) != 0 || valid_name(entry->d_name + 4) == 0){
            continue;
        }
        if(build_path(path, sizeof(path), "%s/%s", projects_path, entry->d_name) != 0 ||
           build_path(hidden, sizeof(hidden), "%s/.%s.deleting.%ld",
                      projects_path, entry->d_name, (long)getpid()) != 0){
            fprintf(stderr, "error: path is too long\n");
            result = 1;
            continue;
        }
        if(lstat(path, &info) != 0){
            perror(path);
            result = 1;
            continue;
        }
        if(S_ISDIR(info.st_mode) == 0 && S_ISLNK(info.st_mode) == 0){
            continue;
        }
        if(rename(path, hidden) != 0){
            perror(path);
            result = 1;
            continue;
        }
        if(meta_forget(entry->d_name + 4, 0) != 0){
            fprintf(stderr, "warning: could not update the index for \"%s\"\n", entry->d_name + 4);
        }
        if(remove_tree(hidden) != 0){
            perror(hidden);
            result = 1;
            continue;
        }
        (*removed)++;
    }
    closedir(dir);
    return result;
}

/*
 * Ask before a deletion that throws away a whole history. Without a
 * terminal the answer cannot be trusted, so --yes is required there.
 */
static int confirm_delete(const char *what, int assumed_yes){
    char answer[16];

    if(assumed_yes){
        return 0;
    }
    if(isatty(STDIN_FILENO) == 0){
        fprintf(stderr, "error: refusing to delete %s without a terminal\n", what);
        fprintf(stderr, "pass --yes to confirm this deletion\n");
        return -1;
    }
    fprintf(stderr, "about to delete %s\n", what);
    fprintf(stderr, "type \"yes\" to continue: ");
    fflush(stderr);
    if(fgets(answer, sizeof(answer), stdin) == NULL){
        fprintf(stderr, "\ncancelled\n");
        return -1;
    }
    if(strcmp(answer, "yes\n") != 0 && strcmp(answer, "yes") != 0){
        fprintf(stderr, "cancelled\n");
        return -1;
    }
    return 0;
}

/*
 * Return 1 when the index still has a record for a project.
 */
static int project_indexed(const char *name){
    vmd_index index;
    int found;

    if(index_load(&index) != 0){
        return 0;
    }
    found = index_count(&index, name) > 0;
    index_free(&index);
    return found;
}

/*
 * Run "vmd rm": remove one version, one project, or every project.
 *
 * A version folder is first renamed to a hidden name in the same directory
 * (atomic, same file system), then the index is updated, then the files are
 * deleted. A crash between the first two steps leaves an index record with
 * no folder; the !on_disk branch below cleans that orphan up on the next rm.
 */
int rm_cmd(int count, const char *const *args, int assumed_yes){
    const char *home = vmd_home();
    char project[PATH_MAX];
    char projects[PATH_MAX];
    char version_path[PATH_MAX];
    char hidden[PATH_MAX];
    char detail[PATH_MAX + 32];
    struct stat info;
    const char *name;
    int version = 0;
    int all_versions = 0;
    int on_disk;

    if(count < 1 || args == NULL || args[0] == NULL){
        fprintf(stderr, "error: wrong arguments for \"rm\"\n");
        fprintf(stderr, "usage: vmd rm <project> <version|all> [--yes]\n");
        fprintf(stderr, "       vmd rm all [--yes]\n");
        return 1;
    }
    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }

    if(count == 1 && strcmp(args[0], "all") == 0){
        int removed = 0;

        if(confirm_delete("every saved project", assumed_yes) != 0){
            return 1;
        }
        if(remove_all_projects(home, &removed) != 0){
            fprintf(stderr, "error: could not remove every project (%d removed)\n", removed);
            return 1;
        }
        if(meta_clear() != 0){
            fprintf(stderr, "warning: could not clear the index; it may still list deleted projects\n");
        }
        printf("removed %d project%s\n", removed, removed == 1 ? "" : "s");
        return 0;
    }

    name = args[0];
    if(valid_name(name) == 0){
        fprintf(stderr, "error: invalid project name\n");
        fprintf(stderr, "use letters, numbers, '-' or '_' (1-%d characters); \"all\" is reserved\n",
                VMD_NAME_MAX - 1);
        return 1;
    }
    if(count == 2){
        if(strcmp(args[1], "all") == 0){
            all_versions = 1;
        }else{
            version = parse_version(args[1]);
            if(version <= 0){
                fprintf(stderr, "error: the version must be a positive number or \"all\"\n");
                return 1;
            }
        }
    }
    if(count == 1){
        fprintf(stderr, "error: \"vmd rm %s\" would delete every version of \"%s\"\n", name, name);
        fprintf(stderr, "use \"vmd rm %s <version>\" to delete one version,\n", name);
        fprintf(stderr, "or  \"vmd rm %s all\" to delete the whole project\n", name);
        return 1;
    }
    if(build_path(project, sizeof(project), "%s/vmd/projects/vmd-%s", home, name) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }

    if(all_versions){
        if(build_path(detail, sizeof(detail), "every version of project \"%s\"", name) != 0){
            fprintf(stderr, "error: path is too long\n");
            return 1;
        }
        on_disk = (lstat(project, &info) == 0);
        if(!on_disk && project_indexed(name) == 0){
            fprintf(stderr, "error: project \"%s\" has not been added\n", name);
            return 1;
        }
        if(confirm_delete(detail, assumed_yes) != 0){
            return 1;
        }
        if(build_path(projects, sizeof(projects), "%s/vmd/projects", home) != 0 ||
           build_path(hidden, sizeof(hidden), "%s/.vmd-%s.deleting.%ld",
                      projects, name, (long)getpid()) != 0){
            fprintf(stderr, "error: path is too long\n");
            return 1;
        }
        if(on_disk && rename(project, hidden) != 0){
            perror(project);
            fprintf(stderr, "error: could not hide \"%s\" before deleting it\n", name);
            return 1;
        }
        if(meta_forget(name, 0) != 0){
            fprintf(stderr, "warning: could not update the index; it may still list this project\n");
        }
        if(on_disk && remove_tree(hidden) != 0){
            fprintf(stderr, "error: \"%s\" was removed from the store, but its files remain at %s\n",
                    name, hidden);
            return 1;
        }
        printf("removed project \"%s\" (all versions)\n", name);
        return 0;
    }

    if(build_path(version_path, sizeof(version_path), "%s/v%d", project, version) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    on_disk = (lstat(version_path, &info) == 0 && S_ISDIR(info.st_mode));
    if(!on_disk){
        meta_forget(name, version);
        fprintf(stderr, "error: version v%d of \"%s\" has not been added\n", version, name);
        return 1;
    }
    if(build_path(hidden, sizeof(hidden), "%s/.v%d.deleting.%ld",
                  project, version, (long)getpid()) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(rename(version_path, hidden) != 0){
        perror(version_path);
        fprintf(stderr, "error: could not hide \"%s\" v%d before deleting it\n", name, version);
        return 1;
    }
    if(meta_forget(name, version) != 0){
        fprintf(stderr, "warning: could not update the index; it may still list v%d\n", version);
    }
    if(remove_tree(hidden) != 0){
        fprintf(stderr, "error: \"%s\" v%d was removed from the index, but its files remain at %s\n",
                name, version, hidden);
        return 1;
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
