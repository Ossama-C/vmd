#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vmd.h"

/* A growing list of project names. */
typedef struct {
    char (*items)[VMD_NAME_MAX];
    int count;
    int capacity;
} name_list;

/* A growing list of version numbers. */
typedef struct {
    int *items;
    int count;
    int capacity;
} number_list;

/*
 * Append one project name to the list.
 */
static int names_add(name_list *list, const char *name){
    if(list->count == list->capacity){
        int capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        char (*items)[VMD_NAME_MAX] = realloc(list->items,
                                              (size_t)capacity * sizeof(*items));

        if(items == NULL){
            return -1;
        }
        list->items = items;
        list->capacity = capacity;
    }
    snprintf(list->items[list->count], VMD_NAME_MAX, "%s", name);
    list->count++;
    return 0;
}

/*
 * Append one version number to the list.
 */
static int numbers_add(number_list *list, int version){
    if(list->count == list->capacity){
        int capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        int *items = realloc(list->items, (size_t)capacity * sizeof(*items));

        if(items == NULL){
            return -1;
        }
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = version;
    return 0;
}

/*
 * qsort comparator for project names.
 */
static int compare_names(const void *left, const void *right){
    const char *a = left;
    const char *b = right;
    return strcmp(a, b);
}

/*
 * qsort comparator for version numbers.
 */
static int compare_numbers(const void *left, const void *right){
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

/*
 * Collect the vmd-* folders that hold at least one version folder.
 */
static int load_projects(const char *home, name_list *names){
    char projects[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if(build_path(projects, sizeof(projects), "%s/vmd/projects", home) != 0){
        return -1;
    }
    dir = opendir(projects);
    if(dir == NULL){
        return errno == ENOENT ? 0 : -1;
    }
    while((entry = readdir(dir)) != NULL){
        char path[PATH_MAX];
        struct stat info;

        if(strncmp(entry->d_name, "vmd-", 4) != 0 || valid_name(entry->d_name + 4) == 0){
            continue;
        }
        if(build_path(path, sizeof(path), "%s/%s", projects, entry->d_name) != 0){
            continue;
        }
        if(lstat(path, &info) != 0 || S_ISDIR(info.st_mode) == 0){
            continue;
        }
        if(names_add(names, entry->d_name + 4) != 0){
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

/*
 * Collect the vN folders of one project.
 */
static int load_versions(const char *home, const char *name, number_list *versions){
    char project[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if(build_path(project, sizeof(project), "%s/vmd/projects/vmd-%s", home, name) != 0){
        return -1;
    }
    dir = opendir(project);
    if(dir == NULL){
        return errno == ENOENT ? 0 : -1;
    }
    while((entry = readdir(dir)) != NULL){
        int version = version_number(entry->d_name);
        char path[PATH_MAX];
        struct stat info;

        if(version <= 0){
            continue;
        }
        if(build_path(path, sizeof(path), "%s/%s", project, entry->d_name) != 0){
            continue;
        }
        if(lstat(path, &info) != 0 || S_ISDIR(info.st_mode) == 0){
            continue;
        }
        if(numbers_add(versions, version) != 0){
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

/*
 * Print every project with its version count and newest date.
 */
static int list_projects(void){
    const char *home = vmd_home();
    name_list names = {0};
    vmd_index index;
    int result = 0;

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(index_load(&index) != 0){
        fprintf(stderr, "warning: could not read the index\n");
    }
    if(load_projects(home, &names) != 0){
        fprintf(stderr, "warning: could not scan the projects folder\n");
    }
    if(names.count == 0){
        printf("no project has been added yet\n");
        printf("use \"vmd add <name>\" inside your project folder\n");
        index_free(&index);
        free(names.items);
        return 0;
    }
    qsort(names.items, (size_t)names.count, sizeof(*names.items), compare_names);

    printf("%-24s %-9s %-7s %s\n", "PROJECT", "VERSIONS", "LAST", "UPDATED");
    for(int i = 0; i < names.count; i++){
        number_list versions = {0};
        const char *name = names.items[i];
        char date[32];
        char last_name[16];
        int last = 0;

        if(load_versions(home, name, &versions) != 0){
            fprintf(stderr, "warning: could not scan project \"%s\"\n", name);
            result = 1;
        }
        if(versions.count == 0){
            printf("%-24s %-9d %-7s %s\n", name, 0, "-", "no versions");
            free(versions.items);
            continue;
        }
        qsort(versions.items, (size_t)versions.count, sizeof(*versions.items),
              compare_numbers);
        last = versions.items[versions.count - 1];
        snprintf(last_name, sizeof(last_name), "v%d", last);

        int position = index_find(&index, name, last);
        if(position < 0){
            printf("%-24s %-9d %-7s %s\n", name, versions.count, last_name,
                   "no metadata (vmd info --rebuild)");
        }else{
            format_time(index.items[position].epoch, date, sizeof(date));
            printf("%-24s %-9d %-7s %s\n", name, versions.count, last_name, date);
        }
        free(versions.items);
    }

    index_free(&index);
    free(names.items);
    return result;
}

/*
 * Print every version of one project.
 */
static int list_versions(const char *name){
    const char *home = vmd_home();
    char project[PATH_MAX];
    struct stat info;
    number_list versions = {0};
    vmd_index index;
    int project_exists;
    int result = 0;

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(valid_name(name) == 0){
        fprintf(stderr, "error: invalid project name\n");
        return 1;
    }
    if(build_path(project, sizeof(project), "%s/vmd/projects/vmd-%s", home, name) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    project_exists = (lstat(project, &info) == 0 && S_ISDIR(info.st_mode));

    if(index_load(&index) != 0){
        fprintf(stderr, "warning: could not read the index\n");
    }
    if(load_versions(home, name, &versions) != 0){
        fprintf(stderr, "warning: could not scan project \"%s\"\n", name);
        result = 1;
    }
    if(versions.count == 0){
        if(project_exists){
            printf("project \"%s\" has no versions\n", name);
        }else{
            printf("the project \"%s\" has not been added\n", name);
        }
        index_free(&index);
        free(versions.items);
        return project_exists ? result : 1;
    }
    qsort(versions.items, (size_t)versions.count, sizeof(*versions.items), compare_numbers);

    printf("project: %s\n\n", name);
    printf("%-8s %-16s %7s %11s  %s\n", "VERSION", "DATE", "FILES", "SIZE", "MESSAGE");
    for(int i = 0; i < versions.count; i++){
        char version_name[16];
        char date[32];
        char size[32];
        int position = index_find(&index, name, versions.items[i]);

        snprintf(version_name, sizeof(version_name), "v%d", versions.items[i]);
        if(position < 0){
            printf("%-8s %-16s %7s %11s  %s\n", version_name, "-", "-", "-",
                   "no metadata (vmd info --rebuild)");
            continue;
        }
        vmd_entry *entry = &index.items[position];
        format_time(entry->epoch, date, sizeof(date));
        human_size(entry->bytes, size, sizeof(size));
        printf("%-8s %-16s %7lu %11s  %s\n",
               version_name, date, entry->files, size,
               entry->message[0] != '\0' ? entry->message : "-");
    }

    index_free(&index);
    free(versions.items);
    return result;
}

/*
 * Dispatch "vmd list" and "vmd list <project>".
 */
int list(int argc, char **argv){
    if(argc == 2){
        return list_projects();
    }
    return list_versions(argv[2]);
}
