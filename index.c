#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "vmd.h"

#define INDEX_FIELDS 6

/*
 * Start with an empty index in memory.
 */
static void index_init(vmd_index *index){
    index->items = NULL;
    index->count = 0;
    index->capacity = 0;
}

/*
 * Release the index array and forget its contents.
 */
void index_free(vmd_index *index){
    if(index == NULL){
        return;
    }
    free(index->items);
    index->items = NULL;
    index->count = 0;
    index->capacity = 0;
}

/*
 * Make room for one more entry.
 */
static int index_reserve(vmd_index *index){
    if(index->count < index->capacity){
        return 0;
    }
    int capacity = index->capacity == 0 ? 16 : index->capacity * 2;
    vmd_entry *items = realloc(index->items, (size_t)capacity * sizeof(*items));

    if(items == NULL){
        return -1;
    }
    index->items = items;
    index->capacity = capacity;
    return 0;
}

/*
 * Build the path of the index file, ~/vmd/index.vmd.
 */
int index_path(char *buf, size_t size){
    const char *home = vmd_home();

    if(home == NULL){
        return -1;
    }
    return build_path(buf, size, "%s/vmd/index.vmd", home);
}

/*
 * Remove every entry of a project, or only one version when version is not 0.
 */
static void index_remove(vmd_index *index, const char *project, int version){
    int i = 0;

    while(i < index->count){
        int same_project = (project == NULL) || (strcmp(index->items[i].project, project) == 0);
        int same_version = (version == 0) || (index->items[i].version == version);

        if(same_project && same_version){
            memmove(&index->items[i], &index->items[i + 1],
                    (size_t)(index->count - i - 1) * sizeof(*index->items));
            index->count--;
        }else{
            i++;
        }
    }
}

/*
 * Replace an old project/version line, then append the new one.
 */
static int index_upsert(vmd_index *index, const vmd_entry *entry){
    index_remove(index, entry->project, entry->version);
    if(index_reserve(index) != 0){
        fprintf(stderr, "error: out of memory while updating the index\n");
        return -1;
    }
    index->items[index->count++] = *entry;
    return 0;
}

/*
 * Return the position of one project/version in the index, or -1.
 */
int index_find(const vmd_index *index, const char *project, int version){
    if(index == NULL){
        return -1;
    }
    for(int i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0 &&
           index->items[i].version == version){
            return i;
        }
    }
    return -1;
}

/*
 * Count the indexed versions of a project.
 */
int index_count(const vmd_index *index, const char *project){
    int count = 0;

    if(index == NULL){
        return 0;
    }
    for(int i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0){
            count++;
        }
    }
    return count;
}

/*
 * Return the highest indexed version number of a project.
 */
int index_last_version(const vmd_index *index, const char *project){
    int last = 0;

    if(index == NULL){
        return 0;
    }
    for(int i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0 &&
           index->items[i].version > last){
            last = index->items[i].version;
        }
    }
    return last;
}

/*
 * Return the newest indexed timestamp of a project.
 */
long long index_last_epoch(const vmd_index *index, const char *project){
    long long last = 0;

    if(index == NULL){
        return 0;
    }
    for(int i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0 &&
           index->items[i].epoch > last){
            last = index->items[i].epoch;
        }
    }
    return last;
}

/*
 * Split one tab-separated line into six fields.
 */
static int split_fields(char *line, char *fields[INDEX_FIELDS]){
    int count = 0;
    char *cursor = line;

    while(count < INDEX_FIELDS){
        char *tab;

        fields[count++] = cursor;
        tab = strchr(cursor, '\t');
        if(tab == NULL){
            break;
        }
        *tab = '\0';
        cursor = tab + 1;
    }
    return count;
}

/*
 * Parse a complete non-negative decimal number.
 */
static int parse_number(const char *text, unsigned long long *out){
    char *end;
    unsigned long long value;

    if(text == NULL || text[0] == '\0' || text[0] == '-'){
        return -1;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if(errno != 0 || *end != '\0'){
        return -1;
    }
    *out = value;
    return 0;
}

/*
 * Read the index file into memory. A missing file is an empty index.
 */
int index_load(vmd_index *index){
    char path[PATH_MAX];
    char *line = NULL;
    size_t size = 0;
    ssize_t length;
    FILE *file;
    int result = 0;

    if(index == NULL){
        return -1;
    }
    index_init(index);
    if(index_path(path, sizeof(path)) != 0){
        fprintf(stderr, "error: HOME is not set\n");
        return -1;
    }
    file = fopen(path, "r");
    if(file == NULL){
        if(errno == ENOENT){
            return 0;
        }
        perror(path);
        return -1;
    }

    while((length = getline(&line, &size, file)) != -1){
        char *fields[INDEX_FIELDS];
        unsigned long long version;
        unsigned long long epoch;
        unsigned long long files;
        unsigned long long bytes;
        vmd_entry entry;

        while(length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')){
            line[--length] = '\0';
        }
        if(length == 0 || line[0] == '#'){
            continue;
        }
        if(split_fields(line, fields) != INDEX_FIELDS){
            continue;
        }
        if(valid_name(fields[0]) == 0 ||
           parse_number(fields[1], &version) != 0 || version < 1 || version > 1000000 ||
           parse_number(fields[2], &epoch) != 0 ||
           parse_number(fields[3], &files) != 0 ||
           parse_number(fields[4], &bytes) != 0){
            continue;
        }

        memset(&entry, 0, sizeof(entry));
        snprintf(entry.project, sizeof(entry.project), "%s", fields[0]);
        entry.version = (int)version;
        entry.epoch = (long long)epoch;
        entry.files = (unsigned long)files;
        entry.bytes = bytes;
        snprintf(entry.message, sizeof(entry.message), "%s", fields[5]);
        if(strcmp(entry.message, "-") == 0){
            entry.message[0] = '\0';
        }
        if(index_upsert(index, &entry) != 0){
            result = -1;
            break;
        }
    }

    if(ferror(file)){
        perror(path);
        result = -1;
    }
    free(line);
    fclose(file);
    if(result != 0){
        index_free(index);
    }
    return result;
}

/*
 * Sort the index by project name, then by version.
 */
static int compare_entries(const void *left, const void *right){
    const vmd_entry *a = left;
    const vmd_entry *b = right;
    int by_project = strcmp(a->project, b->project);

    if(by_project != 0){
        return by_project;
    }
    return (a->version > b->version) - (a->version < b->version);
}

/*
 * Write the index through a temporary file and rename it into place.
 * This is the same small trick the version folders use: readers see either
 * the old index or the new one, never half a line.
 */
int index_save(vmd_index *index){
    char path[PATH_MAX];
    char temporary[PATH_MAX];
    char directory[PATH_MAX];
    const char *home = vmd_home();
    FILE *file;
    int failed;

    if(index == NULL || home == NULL){
        return -1;
    }
    if(index->count > 1){
        qsort(index->items, (size_t)index->count, sizeof(*index->items), compare_entries);
    }
    if(build_path(directory, sizeof(directory), "%s/vmd", home) != 0 ||
       build_path(path, sizeof(path), "%s/index.vmd", directory) != 0 ||
       build_path(temporary, sizeof(temporary), "%s.tmp", path) != 0){
        fprintf(stderr, "error: the index path is too long\n");
        return -1;
    }
    if(mkdir_p(directory, 0755) != 0){
        perror(directory);
        return -1;
    }
    file = fopen(temporary, "w");
    if(file == NULL){
        perror(temporary);
        return -1;
    }

    fprintf(file, "# vmd index v1\n");
    fprintf(file, "# project\tversion\tepoch\tfiles\tbytes\tmessage\n");
    for(int i = 0; i < index->count; i++){
        vmd_entry *entry = &index->items[i];

        fprintf(file, "%s\t%d\t%lld\t%lu\t%llu\t%s\n",
                entry->project, entry->version, entry->epoch,
                entry->files, entry->bytes,
                entry->message[0] != '\0' ? entry->message : "-");
    }
    failed = ferror(file);
    if(fclose(file) != 0){
        failed = 1;
    }
    if(failed){
        fprintf(stderr, "error: could not write the index\n");
        unlink(temporary);
        return -1;
    }
    if(rename(temporary, path) != 0){
        perror(path);
        unlink(temporary);
        return -1;
    }
    return 0;
}

/*
 * Record one version in the index after its folder is safely in place.
 */
int meta_record(const char *project, int version, const char *message, const vmd_stats *stats){
    vmd_index index;
    vmd_entry entry;
    int result;

    if(index_load(&index) != 0){
        return -1;
    }
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.project, sizeof(entry.project), "%s", project);
    entry.version = version;
    entry.epoch = (long long)time(NULL);
    if(stats != NULL){
        entry.files = stats->files;
        entry.bytes = stats->bytes;
    }
    copy_message(entry.message, sizeof(entry.message), message);
    result = index_upsert(&index, &entry);
    if(result == 0){
        result = index_save(&index);
    }
    index_free(&index);
    return result;
}

/*
 * Remove one version, or all versions of a project when version is 0.
 */
int meta_forget(const char *project, int version){
    vmd_index index;
    int result;

    if(index_load(&index) != 0){
        return -1;
    }
    index_remove(&index, project, version);
    result = index_save(&index);
    index_free(&index);
    return result;
}

/*
 * Replace the index with an empty one.
 */
int meta_clear(void){
    vmd_index index;

    index_init(&index);
    return index_save(&index);
}

/*
 * Scan one project folder and add its versions to a fresh index.
 */
static int rebuild_project(const char *projects, const char *name,
                           const vmd_index *old, vmd_index *fresh){
    char project[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if(build_path(project, sizeof(project), "%s/vmd-%s", projects, name) != 0){
        return -1;
    }
    dir = opendir(project);
    if(dir == NULL){
        return -1;
    }
    while((entry = readdir(dir)) != NULL){
        int version = version_number(entry->d_name);
        char path[PATH_MAX];
        struct stat info;
        vmd_stats stats = {0};
        vmd_entry record;
        int previous;

        if(version <= 0){
            continue;
        }
        if(build_path(path, sizeof(path), "%s/%s", project, entry->d_name) != 0){
            continue;
        }
        if(lstat(path, &info) != 0 || S_ISDIR(info.st_mode) == 0){
            continue;
        }
        measure_tree(path, &stats);

        memset(&record, 0, sizeof(record));
        snprintf(record.project, sizeof(record.project), "%s", name);
        record.version = version;
        record.epoch = (long long)(stats.newest > 0 ? stats.newest : info.st_mtime);
        record.files = stats.files;
        record.bytes = stats.bytes;

        previous = index_find(old, name, version);
        if(previous >= 0){
            snprintf(record.message, sizeof(record.message), "%s", old->items[previous].message);
        }
        if(index_upsert(fresh, &record) != 0){
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

/*
 * Rebuild the whole index by scanning ~/vmd/projects.
 */
int meta_rebuild(void){
    const char *home = vmd_home();
    char projects[PATH_MAX];
    char path[PATH_MAX];
    vmd_index old;
    vmd_index fresh;
    DIR *dir;
    struct dirent *entry;
    int result = 0;
    int projects_count = 0;

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(build_path(projects, sizeof(projects), "%s/vmd/projects", home) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    if(index_load(&old) != 0){
        index_free(&old);
        index_init(&old);
    }
    index_init(&fresh);

    dir = opendir(projects);
    if(dir == NULL){
        if(errno != ENOENT){
            perror(projects);
            index_free(&old);
            return 1;
        }
    }else{
        while((entry = readdir(dir)) != NULL){
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
            if(rebuild_project(projects, entry->d_name + 4, &old, &fresh) != 0){
                fprintf(stderr, "warning: could not scan %s\n", path);
                result = 1;
            }
        }
        closedir(dir);
    }

    if(index_save(&fresh) != 0){
        result = 1;
    }else{
        for(int i = 0; i < fresh.count; i++){
            int seen = 0;

            for(int j = 0; j < i; j++){
                if(strcmp(fresh.items[i].project, fresh.items[j].project) == 0){
                    seen = 1;
                    break;
                }
            }
            if(seen == 0){
                projects_count++;
            }
        }
        if(index_path(path, sizeof(path)) != 0){
            snprintf(path, sizeof(path), "%s/vmd/index.vmd", home);
        }
        printf("rebuilt index: %s\n", path);
        printf("%d projects, %d versions\n", projects_count, fresh.count);
    }
    index_free(&fresh);
    index_free(&old);
    return result;
}
