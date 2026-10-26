#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "meta.h"

#define INDEX_FIELDS 6

/*
 * Initialize an empty in-memory index.
 */
static void index_init(vmd_index *index){
    index->items = NULL;
    index->count = 0;
    index->capacity = 0;
}

/*
 * Free the index array and reset its counters.
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
 * Grow the index array so it can hold at least the requested entries.
 */
static int index_reserve(vmd_index *index, size_t needed){
    vmd_entry *items;
    size_t capacity;

    if(needed <= index->capacity){
        return 0;
    }
    capacity = index->capacity == 0 ? 16 : index->capacity;
    while(capacity < needed){
        capacity *= 2;
    }
    items = realloc(index->items, capacity * sizeof(*items));
    if(items == NULL){
        return -1;
    }
    index->items = items;
    index->capacity = capacity;
    return 0;
}

/*
 * Build the path of the central index file, ~/vmd/index.vmd.
 */
int index_path(char *buf, size_t size){
    const char *home = vmd_home();

    if(home == NULL){
        return -1;
    }
    return build_path(buf, size, "%s/vmd/index.vmd", home);
}

/*
 * Writers take an exclusive lock so two vmd processes cannot lose each
 * other's index entries. The lock is best-effort: if it cannot be taken
 * (for example when ~/vmd does not exist yet) the command still works.
 */
static int index_lock(void){
    char path[PATH_MAX];
    char lock_path[PATH_MAX];
    int fd;

    if(index_path(path, sizeof(path)) != 0 ||
       build_path(lock_path, sizeof(lock_path), "%s.lock", path) != 0){
        return -1;
    }
    fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if(fd < 0){
        return -1;
    }
    if(flock(fd, LOCK_EX) != 0){
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * Release an index lock and close its file descriptor.
 */
static void index_unlock(int fd){
    if(fd < 0){
        return;
    }
    flock(fd, LOCK_UN);
    close(fd);
}

/*
 * Parse a complete base-10 signed integer, rejecting junk and overflow.
 */
static int parse_long_long(const char *text, long long *out){
    char *end;
    long long value;

    if(text == NULL || text[0] == '\0'){
        return -1;
    }
    errno = 0;
    value = strtoll(text, &end, 10);
    if(errno != 0 || *end != '\0'){
        return -1;
    }
    *out = value;
    return 0;
}

/*
 * Parse a complete non-negative base-10 unsigned long.
 */
static int parse_unsigned_long(const char *text, unsigned long *out){
    char *end;
    unsigned long value;

    if(text == NULL || text[0] == '\0' || text[0] == '-'){
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if(errno != 0 || *end != '\0'){
        return -1;
    }
    *out = value;
    return 0;
}

/*
 * Parse a complete non-negative base-10 unsigned long long.
 */
static int parse_unsigned_long_long(const char *text, unsigned long long *out){
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
 * Split a tab-separated index line into at most six fields in place.
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
 * Append one entry to the index, growing storage if needed.
 */
static int index_push(vmd_index *index, const vmd_entry *entry){
    if(index_reserve(index, index->count + 1) != 0){
        return -1;
    }
    index->items[index->count++] = *entry;
    return 0;
}

/*
 * Remove the entry at position, shifting later entries down.
 */
static void index_delete_at(vmd_index *index, size_t position){
    if(position >= index->count){
        return;
    }
    if(position + 1 < index->count){
        memmove(&index->items[position], &index->items[position + 1],
                (index->count - position - 1) * sizeof(*index->items));
    }
    index->count--;
}

/*
 * Remove entries matching project and version; zero matches any.
 */
static void index_remove(vmd_index *index, const char *project, int version){
    size_t i = 0;

    while(i < index->count){
        int same_project = (project == NULL) || (strcmp(index->items[i].project, project) == 0);
        int same_version = (version == 0) || (index->items[i].version == version);
        if(same_project && same_version){
            index_delete_at(index, i);
        }else{
            i++;
        }
    }
}

/*
 * Replace an existing project/version entry, then append the new one.
 */
static void index_upsert(vmd_index *index, const vmd_entry *entry){
    index_remove(index, entry->project, entry->version);
    if(index_push(index, entry) != 0){
        fprintf(stderr, "error: out of memory while updating the index\n");
    }
}

/*
 * Return the array position of one project/version, or -1.
 */
int index_find(const vmd_index *index, const char *project, int version){
    if(index == NULL){
        return -1;
    }
    for(size_t i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0 &&
           index->items[i].version == version){
            return (int)i;
        }
    }
    return -1;
}

/*
 * Count the indexed versions belonging to a project.
 */
int index_count(const vmd_index *index, const char *project){
    int count = 0;

    if(index == NULL){
        return 0;
    }
    for(size_t i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0){
            count++;
        }
    }
    return count;
}

/*
 * Return the highest indexed version number for a project.
 */
int index_last_version(const vmd_index *index, const char *project){
    int last = 0;

    if(index == NULL){
        return 0;
    }
    for(size_t i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0 &&
           index->items[i].version > last){
            last = index->items[i].version;
        }
    }
    return last;
}

/*
 * Return the newest indexed timestamp for a project.
 */
long long index_last_epoch(const vmd_index *index, const char *project){
    long long last = 0;

    if(index == NULL){
        return 0;
    }
    for(size_t i = 0; i < index->count; i++){
        if(strcmp(index->items[i].project, project) == 0 &&
           index->items[i].epoch > last){
            last = index->items[i].epoch;
        }
    }
    return last;
}

/*
 * Read and validate the index file into memory; a missing file is empty.
 */
int index_load(vmd_index *index){
    char path[PATH_MAX];
    char *line = NULL;
    size_t capacity = 0;
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

    while((length = getline(&line, &capacity, file)) != -1){
        char *fields[INDEX_FIELDS];
        long long version;
        long long epoch;
        unsigned long files;
        unsigned long long bytes;
        vmd_entry entry;
        int count;

        while(length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')){
            line[--length] = '\0';
        }
        if(length == 0 || line[0] == '#'){
            continue;
        }

        count = split_fields(line, fields);
        if(count != INDEX_FIELDS){
            continue;
        }
        if(valid_name(fields[0]) == 0){
            continue;
        }
        if(parse_long_long(fields[1], &version) != 0 || version <= 0 || version > 1000000){
            continue;
        }
        if(parse_long_long(fields[2], &epoch) != 0 || epoch < 0){
            continue;
        }
        if(parse_unsigned_long(fields[3], &files) != 0 ||
           parse_unsigned_long_long(fields[4], &bytes) != 0){
            continue;
        }

        memset(&entry, 0, sizeof(entry));
        snprintf(entry.project, sizeof(entry.project), "%s", fields[0]);
        entry.version = (int)version;
        entry.epoch = epoch;
        entry.files = files;
        entry.bytes = bytes;
        snprintf(entry.message, sizeof(entry.message), "%s", fields[5]);
        if(strcmp(entry.message, "-") == 0){
            entry.message[0] = '\0';
        }

        index_upsert(index, &entry);
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
 * qsort comparator that sorts entries by project name, then version.
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
 * Sort and write the index atomically with fsync, rename, and directory sync.
 */
int index_save(vmd_index *index){
    char path[PATH_MAX];
    char temporary[PATH_MAX];
    char directory[PATH_MAX];
    const char *home = vmd_home();
    FILE *file;

    if(index == NULL || home == NULL){
        return -1;
    }
    if(index->count > 1){
        qsort(index->items, index->count, sizeof(*index->items), compare_entries);
    }
    if(build_path(directory, sizeof(directory), "%s/vmd", home) != 0 ||
       build_path(path, sizeof(path), "%s/index.vmd", directory) != 0 ||
       build_path(temporary, sizeof(temporary), "%s.tmp", path) != 0){
        fprintf(stderr, "error: index path is too long\n");
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
    for(size_t i = 0; i < index->count; i++){
        const vmd_entry *entry = &index->items[i];
        fprintf(file, "%s\t%d\t%lld\t%lu\t%llu\t%s\n",
                entry->project, entry->version, entry->epoch,
                entry->files, entry->bytes,
                entry->message[0] != '\0' ? entry->message : "-");
    }
    if(fflush(file) != 0){
        perror(temporary);
        fclose(file);
        unlink(temporary);
        return -1;
    }
    if(fsync(fileno(file)) != 0){
        perror(temporary);
        fclose(file);
        unlink(temporary);
        return -1;
    }
    if(fclose(file) != 0){
        perror(temporary);
        unlink(temporary);
        return -1;
    }
    if(rename(temporary, path) != 0){
        perror(path);
        unlink(temporary);
        return -1;
    }

    /* make the rename itself durable (best effort; not all filesystems allow it) */
    int dirfd = open(directory, O_RDONLY);
    if(dirfd >= 0){
        (void)fsync(dirfd);
        close(dirfd);
    }
    return 0;
}

/*
 * Insert or update one version in the index under the writer lock.
 */
int meta_record(const char *project, int version, const char *message, const copy_stats *stats){
    vmd_index index;
    vmd_entry entry;
    int result;
    int lock = index_lock();

    if(index_load(&index) != 0){
        index_unlock(lock);
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
    index_upsert(&index, &entry);

    result = index_save(&index);
    index_free(&index);
    index_unlock(lock);
    return result;
}

/*
 * Remove one version, or all versions of a project, from the index.
 */
int meta_forget(const char *project, int version){
    vmd_index index;
    int result;
    int lock = index_lock();

    if(index_load(&index) != 0){
        index_unlock(lock);
        return -1;
    }
    index_remove(&index, project, version);
    result = index_save(&index);
    index_free(&index);
    index_unlock(lock);
    return result;
}

/*
 * Replace the index with an empty one after removing every project.
 */
int meta_clear(void){
    vmd_index index;
    int result;
    int lock = index_lock();

    index_init(&index);
    result = index_save(&index);
    index_unlock(lock);
    return result;
}

/*
 * Scan one project directory and rebuild its index entries from disk.
 */
static int rebuild_project(const char *projects, const char *name, const vmd_index *old, vmd_index *fresh){
    char project_path[PATH_MAX];
    DIR *project;
    struct dirent *entry;

    if(build_path(project_path, sizeof(project_path), "%s/vmd-%s", projects, name) != 0){
        return -1;
    }
    project = opendir(project_path);
    if(project == NULL){
        return -1;
    }

    while((entry = readdir(project)) != NULL){
        int version = version_number(entry->d_name);
        char version_path[PATH_MAX];
        struct stat info;
        copy_stats stats = {0};
        vmd_entry record;
        int previous;

        if(version <= 0){
            continue;
        }
        if(build_path(version_path, sizeof(version_path), "%s/%s", project_path, entry->d_name) != 0){
            continue;
        }
        if(lstat(version_path, &info) != 0 || S_ISDIR(info.st_mode) == 0){
            continue;
        }
        measure_tree(version_path, &stats);

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

        if(index_reserve(fresh, fresh->count + 1) != 0){
            closedir(project);
            return -1;
        }
        index_upsert(fresh, &record);
    }
    closedir(project);
    return 0;
}

/*
 * Rebuild the whole index from saved folders while holding the writer lock.
 */
int meta_rebuild(void){
    const char *home = vmd_home();
    char projects_path[PATH_MAX];
    vmd_index old;
    vmd_index fresh;
    DIR *projects;
    struct dirent *entry;
    int result = 0;
    int lock;

    if(home == NULL){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(build_path(projects_path, sizeof(projects_path), "%s/vmd/projects", home) != 0){
        fprintf(stderr, "error: path is too long\n");
        return 1;
    }
    lock = index_lock();
    if(index_load(&old) != 0){
        index_free(&old);
        index_init(&old);
    }
    index_init(&fresh);

    projects = opendir(projects_path);
    if(projects == NULL){
        if(errno == ENOENT){
            if(index_save(&fresh) != 0){
                result = 1;
            }else{
                printf("no project has been added yet\n");
            }
            index_unlock(lock);
            index_free(&fresh);
            index_free(&old);
            return result;
        }
        perror(projects_path);
        index_unlock(lock);
        index_free(&old);
        return 1;
    }

    while((entry = readdir(projects)) != NULL){
        struct stat info;
        char project_path[PATH_MAX];

        if(strncmp(entry->d_name, "vmd-", 4) != 0 || valid_name(entry->d_name + 4) == 0){
            continue;
        }
        if(build_path(project_path, sizeof(project_path), "%s/%s", projects_path, entry->d_name) != 0){
            continue;
        }
        if(lstat(project_path, &info) != 0 || S_ISDIR(info.st_mode) == 0){
            continue;
        }
        if(rebuild_project(projects_path, entry->d_name + 4, &old, &fresh) != 0){
            fprintf(stderr, "warning: could not scan %s\n", project_path);
            result = 1;
        }
    }
    closedir(projects);

    if(index_save(&fresh) != 0){
        result = 1;
    }else{
        int project_count = 0;
        for(size_t i = 0; i < fresh.count; i++){
            int known = 0;
            for(size_t j = 0; j < i; j++){
                if(strcmp(fresh.items[i].project, fresh.items[j].project) == 0){
                    known = 1;
                    break;
                }
            }
            if(known == 0){
                project_count++;
            }
        }
        char path[PATH_MAX];
        index_path(path, sizeof(path));
        printf("rebuilt index: %s\n", path);
        printf("%d projects, %zu versions\n", project_count, fresh.count);
    }

    index_unlock(lock);
    index_free(&fresh);
    index_free(&old);
    return result;
}
