#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

/*
 * Check that a project name is non-empty, short enough, path-safe, and not reserved.
 */
int valid_name(const char *name){
    size_t len;

    if(name == NULL){
        return 0;
    }
    len = strlen(name);
    if(len == 0 || len >= VMD_NAME_MAX){
        return 0;
    }
    /* "all" is reserved for `vmd rm all` and `vmd rm <project> all`. */
    if(strcmp(name, "all") == 0){
        return 0;
    }
    for(size_t i = 0; i < len; i++){
        unsigned char c = (unsigned char)name[i];
        if(isalnum(c) == 0 && c != '-' && c != '_'){
            return 0;
        }
    }
    return 1;
}

/*
 * Parse a version number in 1..1000000, rejecting signs and junk.
 */
int parse_version(const char *text){
    char *end;
    long value;

    if(text == NULL || isdigit((unsigned char)text[0]) == 0){
        return -1;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if(errno != 0 || *end != '\0' || value <= 0 || value > 1000000){
        return -1;
    }
    return (int)value;
}

/*
 * Return HOME when set, or NULL when the environment is incomplete.
 */
const char *vmd_home(void){
    const char *home = getenv("HOME");

    if(home == NULL || home[0] == '\0'){
        return NULL;
    }
    return home;
}

/*
 * Format a path into buf, returning -1 instead of truncating it.
 */
int build_path(char *buf, size_t size, const char *fmt, ...){
    va_list args;
    int written;

    if(buf == NULL || size == 0){
        return -1;
    }
    va_start(args, fmt);
    written = vsnprintf(buf, size, fmt, args);
    va_end(args);

    if(written < 0 || (size_t)written >= size){
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

/*
 * Create a directory or accept an existing directory; fail on non-directories.
 */
static int ensure_dir(const char *path, mode_t mode){
    struct stat info;

    if(mkdir(path, mode) == 0){
        return 0;
    }
    if(errno != EEXIST){
        return -1;
    }
    if(stat(path, &info) != 0){
        return -1;
    }
    if(S_ISDIR(info.st_mode) == 0){
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

/*
 * Create every missing component of a path, including the root case.
 */
int mkdir_p(const char *path, mode_t mode){
    char tmp[PATH_MAX];
    size_t len;

    if(path == NULL){
        errno = EINVAL;
        return -1;
    }
    len = strlen(path);
    if(len == 0 || len >= sizeof(tmp)){
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(tmp, path, len + 1);
    if(tmp[len - 1] == '/'){
        tmp[len - 1] = '\0';
    }
    if(tmp[0] == '\0'){
        return ensure_dir("/", mode);
    }

    for(char *p = tmp + 1; *p != '\0'; p++){
        if(*p != '/'){
            continue;
        }
        *p = '\0';
        if(tmp[0] != '\0' && ensure_dir(tmp, mode) != 0){
            return -1;
        }
        *p = '/';
    }
    if(ensure_dir(tmp, mode) != 0){
        return -1;
    }
    return 0;
}

/*
 * Lexically normalize an absolute path by resolving "." and ".." components.
 */
static int normalize_absolute(const char *input, char *out, size_t size){
    char work[PATH_MAX];
    size_t length = strlen(input);
    size_t out_length = 0;
    size_t position = 0;

    if(length >= sizeof(work)){
        return -1;
    }
    memcpy(work, input, length + 1);
    if(work[0] != '/'){
        return -1;
    }

    while(work[position] != '\0'){
        size_t start;
        size_t component;

        while(work[position] == '/'){
            position++;
        }
        if(work[position] == '\0'){
            break;
        }
        start = position;
        while(work[position] != '\0' && work[position] != '/'){
            position++;
        }
        component = position - start;
        if(component == 1 && work[start] == '.'){
            continue;
        }
        if(component == 2 && work[start] == '.' && work[start + 1] == '.'){
            while(out_length > 0 && out[out_length - 1] != '/'){
                out_length--;
            }
            if(out_length > 0){
                out_length--;
            }
            out[out_length] = '\0';
            continue;
        }
        if(out_length + component + 2 > size){
            return -1;
        }
        out[out_length++] = '/';
        memcpy(out + out_length, work + start, component);
        out_length += component;
        out[out_length] = '\0';
    }
    if(out_length == 0){
        if(size < 2){
            return -1;
        }
        out[0] = '/';
        out[1] = '\0';
    }
    return 0;
}

/*
 * Resolve the longest existing prefix of a path and normalize the rest.
 * Recursion depth is capped to keep malformed paths from exhausting the stack.
 */
static int resolve_path_depth(const char *path, char *buf, size_t size, int depth){
    char absolute[PATH_MAX];
    char real[PATH_MAX];
    char combined[PATH_MAX];
    char cwd[PATH_MAX];
    char *slash;
    const char *suffix;

    if(depth > 64){
        return -1;
    }
    if(path == NULL || path[0] == '\0' || buf == NULL || size == 0){
        return -1;
    }

    if(path[0] == '/'){
        if(build_path(absolute, sizeof(absolute), "%s", path) != 0){
            return -1;
        }
    }else{
        if(getcwd(cwd, sizeof(cwd)) == NULL){
            return -1;
        }
        if(build_path(absolute, sizeof(absolute), "%s/%s", cwd, path) != 0){
            return -1;
        }
    }

    if(realpath(absolute, real) != NULL){
        return build_path(buf, size, "%s", real);
    }

    slash = strrchr(absolute, '/');
    if(slash == NULL){
        return -1;
    }
    suffix = slash + 1;
    *slash = '\0';

    if(absolute[0] == '\0'){
        if(build_path(combined, sizeof(combined), "/%s", suffix) != 0){
            return -1;
        }
        return normalize_absolute(combined, buf, size);
    }
    if(resolve_path_depth(absolute, real, sizeof(real), depth + 1) != 0){
        return -1;
    }
    if(strcmp(real, "/") == 0){
        if(build_path(combined, sizeof(combined), "/%s", suffix) != 0){
            return -1;
        }
    }else{
        if(build_path(combined, sizeof(combined), "%s/%s", real, suffix) != 0){
            return -1;
        }
    }
    return normalize_absolute(combined, buf, size);
}

/*
 * Resolve and normalize a path without requiring the final component to exist.
 */
int resolve_path(const char *path, char *buf, size_t size){
    return resolve_path_depth(path, buf, size, 0);
}

/*
 * Return 1 when path is equal to dir or lies below it.
 */
int is_inside(const char *path, const char *dir){
    size_t len;

    if(path == NULL || dir == NULL){
        return 0;
    }
    len = strlen(dir);
    if(len == 0){
        return 0;
    }
    if(len == 1 && dir[0] == '/'){
        return path[0] == '/';
    }
    if(strncmp(path, dir, len) != 0){
        return 0;
    }
    return path[len] == '\0' || path[len] == '/';
}

/*
 * Copy one regular file, preserve its mode, refuse self-copies, and update stats.
 */
static int copy_file(const char *src, const char *dst, const struct stat *info, copy_stats *stats){
    int in = -1;
    int out = -1;
    int result = -1;
    char buffer[65536];
    ssize_t got;
    struct stat src_info;
    struct stat dst_info;

    in = open(src, O_RDONLY);
    if(in < 0){
        perror(src);
        return -1;
    }
    if(fstat(in, &src_info) != 0){
        perror(src);
        close(in);
        return -1;
    }
    if(lstat(dst, &dst_info) == 0){
        if(S_ISDIR(dst_info.st_mode)){
            fprintf(stderr, "error: %s is a directory\n", dst);
            close(in);
            return -1;
        }
        if(S_ISLNK(dst_info.st_mode)){
            if(unlink(dst) != 0){
                perror(dst);
                close(in);
                return -1;
            }
        }else if(S_ISREG(dst_info.st_mode)){
            if(dst_info.st_dev == src_info.st_dev && dst_info.st_ino == src_info.st_ino){
                fprintf(stderr, "error: refusing to copy a file onto itself: %s\n", dst);
                close(in);
                return -1;
            }
            if(unlink(dst) != 0){
                perror(dst);
                close(in);
                return -1;
            }
        }else{
            fprintf(stderr, "error: %s is not a regular file\n", dst);
            close(in);
            return -1;
        }
    }else if(errno != ENOENT){
        perror(dst);
        close(in);
        return -1;
    }
    out = open(dst, O_WRONLY | O_CREAT | O_EXCL, info->st_mode & 0777);
    if(out < 0){
        perror(dst);
        close(in);
        return -1;
    }

    while((got = read(in, buffer, sizeof(buffer))) > 0){
        ssize_t offset = 0;
        while(offset < got){
            ssize_t written = write(out, buffer + offset, (size_t)(got - offset));
            if(written < 0){
                if(errno == EINTR){
                    continue;
                }
                perror(dst);
                goto done;
            }
            if(written == 0){
                fprintf(stderr, "error: could not write %s\n", dst);
                goto done;
            }
            offset += written;
        }
    }
    if(got < 0){
        perror(src);
        goto done;
    }
    if(fchmod(out, info->st_mode & 0777) != 0){
        perror(dst);
        goto done;
    }
    result = 0;

done:
    close(in);
    if(close(out) != 0 && result == 0){
        perror(dst);
        result = -1;
    }
    if(result != 0){
        unlink(dst);
    }else if(stats != NULL){
        stats->files++;
        stats->bytes += (unsigned long long)info->st_size;
        if(info->st_mtime > stats->newest){
            stats->newest = info->st_mtime;
        }
    }
    return result;
}

/*
 * Recreate a symbolic link at dst and count it as one file.
 */
static int copy_link(const char *src, const char *dst, const struct stat *info, copy_stats *stats){
    char *target;
    ssize_t length;

    (void)info;
    target = malloc(PATH_MAX);
    if(target == NULL){
        perror("malloc");
        return -1;
    }
    length = readlink(src, target, PATH_MAX - 1);
    if(length < 0){
        perror(src);
        free(target);
        return -1;
    }
    target[length] = '\0';

    if(unlink(dst) != 0 && errno != ENOENT){
        perror(dst);
        free(target);
        return -1;
    }
    if(symlink(target, dst) != 0){
        perror(dst);
        free(target);
        return -1;
    }
    free(target);

    if(stats != NULL){
        stats->files++;
    }
    return 0;
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ignore_list;

/*
 * Free all patterns in an ignore list.
 */
static void ignore_free(ignore_list *ignore){
    for(size_t i = 0; i < ignore->count; i++){
        free(ignore->items[i]);
    }
    free(ignore->items);
    ignore->items = NULL;
    ignore->count = 0;
    ignore->capacity = 0;
}

/*
 * Append a copy of one ignore pattern, growing the list if needed.
 */
static int ignore_add(ignore_list *ignore, const char *pattern){
    char *copy;

    if(ignore->count == ignore->capacity){
        size_t capacity = ignore->capacity == 0 ? 8 : ignore->capacity * 2;
        char **items = realloc(ignore->items, capacity * sizeof(*items));
        if(items == NULL){
            return -1;
        }
        ignore->items = items;
        ignore->capacity = capacity;
    }
    copy = malloc(strlen(pattern) + 1);
    if(copy == NULL){
        return -1;
    }
    strcpy(copy, pattern);
    ignore->items[ignore->count++] = copy;
    return 0;
}

/*
 * Read .vmdignore from a project root and store its patterns.
 */
static int ignore_load(ignore_list *ignore, const char *root){
    char path[PATH_MAX];
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    FILE *file;

    ignore->items = NULL;
    ignore->count = 0;
    ignore->capacity = 0;

    if(build_path(path, sizeof(path), "%s/.vmdignore", root) != 0){
        return -1;
    }
    file = fopen(path, "r");
    if(file == NULL){
        return errno == ENOENT ? 0 : -1;
    }

    while((length = getline(&line, &capacity, file)) != -1){
        char *start = line;
        char *end;

        while(length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')){
            line[--length] = '\0';
        }
        while(*start == ' ' || *start == '\t'){
            start++;
        }
        end = start + strlen(start);
        while(end > start && (end[-1] == ' ' || end[-1] == '\t')){
            *--end = '\0';
        }
        if(*start == '\0' || *start == '#' || *start == '!'){
            continue;
        }
        if(ignore_add(ignore, start) != 0){
            free(line);
            fclose(file);
            ignore_free(ignore);
            return -1;
        }
    }
    free(line);
    fclose(file);
    return 0;
}

/*
 * Return 1 when a relative path matches any ignore pattern.
 */
static int ignore_match(const ignore_list *ignore, const char *relative, int is_dir){
    const char *base = strrchr(relative, '/');
    char pattern[PATH_MAX];

    base = base == NULL ? relative : base + 1;
    for(size_t i = 0; i < ignore->count; i++){
        const char *item = ignore->items[i];
        size_t length = strlen(item);
        int directory_only = (length > 0 && item[length - 1] == '/');

        if(directory_only && is_dir == 0){
            continue;
        }
        if(length >= sizeof(pattern)){
            continue;
        }
        memcpy(pattern, item, length + 1);
        if(directory_only){
            pattern[length - 1] = '\0';
        }
        if(strchr(pattern, '/') != NULL){
            if(fnmatch(pattern, relative, FNM_PATHNAME) == 0){
                return 1;
            }
        }else if(fnmatch(pattern, base, 0) == 0){
            return 1;
        }
    }
    return 0;
}

#define VMD_MAX_DEPTH 256

/*
 * Recursively copy a tree, honoring ignore rules, symlinks, and a depth cap.
 */
static int copy_tree_inner(const char *src, const char *dst, const char *relative,
                           const ignore_list *ignore, copy_stats *stats, int depth){
    struct stat info;

    if(depth > VMD_MAX_DEPTH){
        fprintf(stderr, "error: directory tree is too deep (limit %d): %s\n",
                VMD_MAX_DEPTH, src);
        return -1;
    }
    if(lstat(src, &info) != 0){
        perror(src);
        return -1;
    }

    if(ignore != NULL && relative[0] != '\0' &&
       ignore_match(ignore, relative, S_ISDIR(info.st_mode))){
        if(stats != NULL){
            stats->skipped++;
        }
        return 0;
    }

    if(S_ISLNK(info.st_mode)){
        return copy_link(src, dst, &info, stats);
    }

    if(S_ISREG(info.st_mode)){
        return copy_file(src, dst, &info, stats);
    }

    if(S_ISDIR(info.st_mode)){
        DIR *dir;
        struct dirent *entry;
        struct stat dst_info;
        int existed;

        dir = opendir(src);
        if(dir == NULL){
            perror(src);
            return -1;
        }
        existed = (lstat(dst, &dst_info) == 0);
        if(ensure_dir(dst, 0700) != 0){
            perror(dst);
            closedir(dir);
            return -1;
        }

        for(;;){
            char child_src[PATH_MAX];
            char child_dst[PATH_MAX];
            char child_relative[PATH_MAX];

            errno = 0;
            entry = readdir(dir);
            if(entry == NULL){
                if(errno != 0){
                    perror(src);
                    closedir(dir);
                    return -1;
                }
                break;
            }
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            if(build_path(child_src, sizeof(child_src), "%s/%s", src, entry->d_name) != 0 ||
               build_path(child_dst, sizeof(child_dst), "%s/%s", dst, entry->d_name) != 0 ||
               (relative[0] != '\0'
                    ? build_path(child_relative, sizeof(child_relative), "%s/%s", relative, entry->d_name) != 0
                    : build_path(child_relative, sizeof(child_relative), "%s", entry->d_name) != 0)){
                fprintf(stderr, "error: path is too long under %s\n", dst);
                closedir(dir);
                return -1;
            }
            if(copy_tree_inner(child_src, child_dst, child_relative, ignore, stats, depth + 1) != 0){
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);

        if(existed == 0 && chmod(dst, info.st_mode & 0777) != 0){
            perror(dst);
            return -1;
        }
        return 0;
    }

    fprintf(stderr, "warning: skipping unsupported file %s\n", src);
    return 0;
}

/*
 * Copy a tree without ignore rules.
 */
int copy_tree(const char *src, const char *dst, copy_stats *stats){
    return copy_tree_inner(src, dst, "", NULL, stats, 0);
}

/*
 * Copy a project tree using the .vmdignore file at its root.
 */
int copy_project(const char *src, const char *dst, copy_stats *stats){
    ignore_list ignore;
    int result;

    if(ignore_load(&ignore, src) != 0){
        fprintf(stderr, "warning: could not read %s/.vmdignore; copying everything\n", src);
        ignore_free(&ignore);
        ignore.items = NULL;
        ignore.count = 0;
        ignore.capacity = 0;
    }
    result = copy_tree_inner(src, dst, "", &ignore, stats, 0);
    ignore_free(&ignore);
    return result;
}

/*
 * Recursively delete a path, making directories writable first, with a depth cap.
 */
static int remove_tree_inner(const char *path, int depth){
    struct stat info;

    if(lstat(path, &info) != 0){
        if(errno == ENOENT){
            return 0;
        }
        perror(path);
        return -1;
    }

    if(S_ISDIR(info.st_mode)){
        DIR *dir;
        struct dirent *entry;

        if(depth > VMD_MAX_DEPTH){
            fprintf(stderr, "error: directory tree is too deep (limit %d): %s\n",
                    VMD_MAX_DEPTH, path);
            return -1;
        }
        /* make read-only directories writable so their entries can be removed */
        if(chmod(path, 0700) != 0){
            perror(path);
            return -1;
        }
        dir = opendir(path);
        if(dir == NULL){
            perror(path);
            return -1;
        }
        for(;;){
            char child[PATH_MAX];

            errno = 0;
            entry = readdir(dir);
            if(entry == NULL){
                if(errno != 0){
                    perror(path);
                    closedir(dir);
                    return -1;
                }
                break;
            }
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            if(build_path(child, sizeof(child), "%s/%s", path, entry->d_name) != 0){
                fprintf(stderr, "error: path is too long under %s\n", path);
                closedir(dir);
                return -1;
            }
            if(remove_tree_inner(child, depth + 1) != 0){
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        if(rmdir(path) != 0){
            perror(path);
            return -1;
        }
        return 0;
    }

    if(remove(path) != 0){
        perror(path);
        return -1;
    }
    return 0;
}

/*
 * Delete a file or directory tree; a missing path counts as success.
 */
int remove_tree(const char *path){
    if(path == NULL){
        return 0;
    }
    return remove_tree_inner(path, 0);
}

/*
 * Recursively count files, bytes, and the newest mtime in a tree.
 */
static int measure_tree_inner(const char *path, copy_stats *stats, int depth){
    struct stat info;

    if(depth > VMD_MAX_DEPTH){
        fprintf(stderr, "error: directory tree is too deep (limit %d): %s\n",
                VMD_MAX_DEPTH, path);
        return -1;
    }
    if(stats == NULL){
        return -1;
    }
    if(lstat(path, &info) != 0){
        return -1;
    }
    if(info.st_mtime > stats->newest){
        stats->newest = info.st_mtime;
    }

    if(S_ISREG(info.st_mode)){
        stats->files++;
        stats->bytes += (unsigned long long)info.st_size;
        return 0;
    }
    if(S_ISLNK(info.st_mode)){
        stats->files++;
        return 0;
    }
    if(S_ISDIR(info.st_mode)){
        DIR *dir = opendir(path);
        struct dirent *entry;

        if(dir == NULL){
            return -1;
        }
        for(;;){
            char child[PATH_MAX];

            errno = 0;
            entry = readdir(dir);
            if(entry == NULL){
                if(errno != 0){
                    perror(path);
                    closedir(dir);
                    return -1;
                }
                break;
            }
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            if(build_path(child, sizeof(child), "%s/%s", path, entry->d_name) != 0){
                closedir(dir);
                return -1;
            }
            if(measure_tree_inner(child, stats, depth + 1) != 0){
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
    }
    return 0;
}

/*
 * Measure a tree for rebuild metadata.
 */
int measure_tree(const char *path, copy_stats *stats){
    return measure_tree_inner(path, stats, 0);
}

/*
 * Parse a "vN" directory name and return N, or -1.
 */
int version_number(const char *name){
    if(name == NULL || name[0] != 'v'){
        return -1;
    }
    return parse_version(name + 1);
}

/*
 * Return one more than the highest existing vN directory.
 */
int next_version(const char *project_path){
    DIR *dir;
    struct dirent *entry;
    int highest = 0;

    dir = opendir(project_path);
    if(dir == NULL){
        return -1;
    }
    while((entry = readdir(dir)) != NULL){
        int version = version_number(entry->d_name);
        char child[PATH_MAX];
        struct stat info;

        if(version <= highest){
            continue;
        }
        if(build_path(child, sizeof(child), "%s/%s", project_path, entry->d_name) != 0){
            continue;
        }
        if(lstat(child, &info) == 0 && S_ISDIR(info.st_mode)){
            highest = version;
        }
    }
    closedir(dir);
    return highest + 1;
}

/*
 * Format a Unix timestamp as local "YYYY-MM-DD HH:MM".
 */
void format_time(long long epoch, char *buf, size_t size){
    time_t value = (time_t)epoch;
    struct tm info;

    if(buf == NULL || size == 0){
        return;
    }
    if(epoch <= 0 || localtime_r(&value, &info) == NULL ||
       strftime(buf, size, "%Y-%m-%d %H:%M", &info) == 0){
        snprintf(buf, size, "unknown");
    }
}

/*
 * Format a byte count with binary units (B, KiB, MiB, ...).
 */
void human_size(unsigned long long bytes, char *buf, size_t size){
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = (double)bytes;
    int unit = 0;

    while(value >= 1024.0 && unit < 4){
        value /= 1024.0;
        unit++;
    }
    if(unit == 0){
        snprintf(buf, size, "%llu B", bytes);
    }else{
        snprintf(buf, size, "%.1f %s", value, units[unit]);
    }
}

/*
 * Trim, sanitize, and truncate a message so it is safe for one index line.
 */
void copy_message(char *dst, size_t size, const char *message){
    size_t i = 0;

    if(dst == NULL || size == 0){
        return;
    }
    if(message == NULL){
        dst[0] = '\0';
        return;
    }
    while(*message == ' ' || *message == '\t'){
        message++;
    }
    while(message[i] != '\0' && i + 1 < size){
        unsigned char c = (unsigned char)message[i];
        dst[i] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : (char)c;
        i++;
    }
    while(i > 0 && dst[i - 1] == ' '){
        i--;
    }
    dst[i] = '\0';
}
