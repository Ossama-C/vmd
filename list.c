#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "list.h"
#include "meta.h"
#include "util.h"

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} name_list;

typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} version_list;

/*
 * Free every stored project name and reset the list.
 */
static void names_free(name_list *list){
    for(size_t i = 0; i < list->count; i++){
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/*
 * Add a project name once, growing and de-duplicating the list.
 */
static int names_add(name_list *list, const char *name){
    char *copy;

    for(size_t i = 0; i < list->count; i++){
        if(strcmp(list->items[i], name) == 0){
            return 0;
        }
    }
    if(list->count == list->capacity){
        size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        char **items = realloc(list->items, capacity * sizeof(*items));
        if(items == NULL){
            return -1;
        }
        list->items = items;
        list->capacity = capacity;
    }
    copy = malloc(strlen(name) + 1);
    if(copy == NULL){
        return -1;
    }
    strcpy(copy, name);
    list->items[list->count++] = copy;
    return 0;
}

/*
 * Add a version number once, growing and de-duplicating the list.
 */
static int versions_add(version_list *list, int version){
    for(size_t i = 0; i < list->count; i++){
        if(list->items[i] == version){
            return 0;
        }
    }
    if(list->count == list->capacity){
        size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        int *items = realloc(list->items, capacity * sizeof(*items));
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
 * qsort comparator for project-name pointers.
 */
static int compare_names(const void *left, const void *right){
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;
    return strcmp(a, b);
}

/*
 * qsort comparator for integer version numbers.
 */
static int compare_versions(const void *left, const void *right){
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

/*
 * Build the absolute path of one project directory.
 */
static int project_path(char *buf, size_t size, const char *home, const char *name){
    return build_path(buf, size, "%s/vmd/projects/vmd-%s", home, name);
}

/*
 * Discover valid vmd-* project directories and add their names.
 */
static void load_projects_from_disk(const char *home, name_list *names){
    char projects[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if(build_path(projects, sizeof(projects), "%s/vmd/projects", home) != 0){
        return;
    }
    dir = opendir(projects);
    if(dir == NULL){
        return;
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
        names_add(names, entry->d_name + 4);
    }
    closedir(dir);
}

/*
 * Discover valid vN directories inside one project.
 */
static void load_versions_from_disk(const char *home, const char *name, version_list *versions){
    char path[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if(project_path(path, sizeof(path), home, name) != 0){
        return;
    }
    dir = opendir(path);
    if(dir == NULL){
        return;
    }
    while((entry = readdir(dir)) != NULL){
        int version = version_number(entry->d_name);
        char child[PATH_MAX];
        struct stat info;

        if(version <= 0){
            continue;
        }
        if(build_path(child, sizeof(child), "%s/%s", path, entry->d_name) != 0){
            continue;
        }
        if(lstat(child, &info) == 0 && S_ISDIR(info.st_mode)){
            versions_add(versions, version);
        }
    }
    closedir(dir);
}

/*
 * Print all projects with their version count and newest date.
 */
static int list_projects(void){
    const char *home = vmd_home();
    vmd_index index;
    name_list names = {0};
    int result = 0;

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(index_load(&index) != 0){
        fprintf(stderr, "warning: could not read the index\n");
    }
    for(size_t i = 0; i < index.count; i++){
        names_add(&names, index.items[i].project);
    }
    load_projects_from_disk(home, &names);

    if(names.count == 0){
        printf("no project has been added yet\n");
        printf("use \"vmd add <name>\" inside your project folder\n");
        index_free(&index);
        names_free(&names);
        return 0;
    }
    qsort(names.items, names.count, sizeof(*names.items), compare_names);

    printf("%-24s %-9s %-7s %s\n", "PROJECT", "VERSIONS", "LAST", "UPDATED");
    for(size_t i = 0; i < names.count; i++){
        int count = index_count(&index, names.items[i]);
        int last = index_last_version(&index, names.items[i]);
        long long epoch = index_last_epoch(&index, names.items[i]);
        char date[32];

        if(count == 0){
            printf("%-24s %-9s %-7s %s\n", names.items[i], "?", "-", "no metadata (vmd info --rebuild)");
            continue;
        }
        format_time(epoch, date, sizeof(date));
        char last_name[16];
        snprintf(last_name, sizeof(last_name), "v%d", last);
        printf("%-24s %-9d %-7s %s\n", names.items[i], count, last_name, date);
    }

    index_free(&index);
    names_free(&names);
    return result;
}

/*
 * Print every version of one project, using index metadata when present.
 */
static int list_versions(const char *name){
    const char *home = vmd_home();
    vmd_index index;
    version_list versions = {0};
    char path[PATH_MAX];
    struct stat info;
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
    if(project_path(path, sizeof(path), home, name) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    project_exists = (lstat(path, &info) == 0 && S_ISDIR(info.st_mode));

    if(index_load(&index) != 0){
        fprintf(stderr, "warning: could not read the index\n");
    }
    for(size_t i = 0; i < index.count; i++){
        if(strcmp(index.items[i].project, name) == 0){
            versions_add(&versions, index.items[i].version);
        }
    }
    load_versions_from_disk(home, name, &versions);

    if(versions.count == 0){
        if(project_exists){
            printf("project \"%s\" has no versions\n", name);
        }else{
            printf("the project \"%s\" has not been added\n", name);
        }
        index_free(&index);
        free(versions.items);
        return project_exists ? 0 : 1;
    }
    qsort(versions.items, versions.count, sizeof(*versions.items), compare_versions);

    printf("project: %s\n\n", name);
    printf("%-8s %-16s %7s %11s  %s\n", "VERSION", "DATE", "FILES", "SIZE", "MESSAGE");
    for(size_t i = 0; i < versions.count; i++){
        int position = index_find(&index, name, versions.items[i]);
        char version_name[16];
        char date[32];
        char size[32];

        snprintf(version_name, sizeof(version_name), "v%d", versions.items[i]);
        if(position < 0){
            printf("%-8s %-16s %7s %11s  %s\n", version_name, "-", "-", "-",
                   "no metadata (vmd info --rebuild)");
            continue;
        }
        const vmd_entry *entry = &index.items[position];
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
