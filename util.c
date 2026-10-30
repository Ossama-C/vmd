#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vmd.h"

#define COPY_BUFFER 65536
#define MAX_DEPTH 64

/* Patterns read from the .vmdignore file of one folder. */
typedef struct {
    char pattern[VMD_PATTERNS][VMD_PATTERN_MAX];
    int count;
} vmd_ignore;

/*
 * Return 1 when a project name is safe to use as a folder name.
 * "all" is reserved by "vmd rm all".
 */
int valid_name(const char *name){
    size_t length;

    if(name == NULL){
        return 0;
    }
    length = strlen(name);
    if(length == 0 || length >= VMD_NAME_MAX || strcmp(name, "all") == 0){
        return 0;
    }
    for(size_t i = 0; i < length; i++){
        unsigned char c = (unsigned char)name[i];
        if(isalnum(c) == 0 && c != '-' && c != '_'){
            return 0;
        }
    }
    return 1;
}

/*
 * Parse a positive version number.
 */
int parse_version(const char *text){
    char *end;
    long value;

    if(text == NULL || text[0] == '\0'){
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
 * Return the HOME directory, or NULL when it is not set.
 */
const char *vmd_home(void){
    const char *home = getenv("HOME");

    if(home == NULL || home[0] == '\0'){
        return NULL;
    }
    return home;
}

/*
 * Format a path and fail instead of silently truncating it.
 */
int build_path(char *buf, size_t size, const char *fmt, ...){
    va_list args;
    int length;

    va_start(args, fmt);
    length = vsnprintf(buf, size, fmt, args);
    va_end(args);
    if(length < 0 || (size_t)length >= size){
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

/*
 * Create one directory, accepting a directory that already exists.
 */
static int make_dir(const char *path, mode_t mode){
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
 * Create a directory and every missing parent.
 */
int mkdir_p(const char *path, mode_t mode){
    char buffer[PATH_MAX];
    size_t length = strlen(path);

    if(length == 0 || length >= sizeof(buffer)){
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buffer, path, length + 1);
    for(char *cursor = buffer + 1; *cursor != '\0'; cursor++){
        if(*cursor != '/'){
            continue;
        }
        *cursor = '\0';
        if(make_dir(buffer, mode) != 0){
            return -1;
        }
        *cursor = '/';
    }
    return make_dir(buffer, mode);
}

/*
 * Return 1 when path is equal to dir or lies below it.
 */
int is_inside(const char *path, const char *dir){
    size_t length = strlen(dir);

    if(strncmp(path, dir, length) != 0){
        return 0;
    }
    return path[length] == '\0' || path[length] == '/';
}

/*
 * Read .vmdignore from the folder being saved into a small fixed list.
 *
 * Bad lines are reported with their line number instead of disappearing:
 * "!" is not supported, and a line that does not fit in VMD_PATTERN_MAX is
 * skipped. When the file asks for more than VMD_PATTERNS patterns the save
 * stops: silently dropping a rule could leave a secret in the snapshot.
 */
static int ignore_load(vmd_ignore *rules, const char *root){
    char path[PATH_MAX];
    char line[VMD_PATTERN_MAX];
    FILE *file;
    int line_number = 0;

    rules->count = 0;
    if(build_path(path, sizeof(path), "%s/.vmdignore", root) != 0){
        return 0;
    }
    file = fopen(path, "r");
    if(file == NULL){
        return 0;
    }
    while(fgets(line, sizeof(line), file) != NULL){
        char *start = line;
        size_t length;

        line_number++;
        if(strchr(line, '\n') == NULL && feof(file) == 0){
            int c;
            while((c = fgetc(file)) != '\n' && c != EOF){
            }
            fprintf(stderr, "warning: %s line %d is too long; skipped\n", path, line_number);
            continue;
        }
        while(*start == ' ' || *start == '\t'){
            start++;
        }
        length = strlen(start);
        while(length > 0 && (start[length - 1] == '\n' || start[length - 1] == '\r' ||
                             start[length - 1] == ' ' || start[length - 1] == '\t')){
            start[--length] = '\0';
        }
        if(length == 0 || start[0] == '#'){
            continue;
        }
        if(start[0] == '!'){
            fprintf(stderr, "warning: %s line %d: \"!\" patterns are not supported; skipped\n",
                    path, line_number);
            continue;
        }
        if(rules->count >= VMD_PATTERNS){
            fprintf(stderr, "error: %s has more than %d patterns; refusing to save\n",
                    path, VMD_PATTERNS);
            fclose(file);
            return -1;
        }
        snprintf(rules->pattern[rules->count], VMD_PATTERN_MAX, "%s", start);
        rules->count++;
    }
    fclose(file);
    return 0;
}

/*
 * Return 1 when a relative path matches one of the ignore patterns.
 * A pattern without '/' matches a name at any depth; a pattern with '/'
 * is relative to the saved folder. A trailing '/' means directories only.
 */
static int ignore_match(const vmd_ignore *rules, const char *relative, int is_dir){
    const char *base = strrchr(relative, '/');
    char pattern[VMD_PATTERN_MAX];

    base = base == NULL ? relative : base + 1;
    for(int i = 0; i < rules->count; i++){
        const char *item = rules->pattern[i];
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

/*
 * Report a copy failure with the operation, the path, and the reason.
 * It is called immediately where the error happens, before close/unlink
 * can overwrite errno.
 */
static void copy_error(const char *action, const char *path){
    fprintf(stderr, "error: cannot %s ", action);
    perror(path);
}

/*
 * Copy one regular file. Saved files use mode 0777: vmd stores the work,
 * not the privileges (setuid/setgid/sticky bits are deliberately dropped).
 */
static int copy_file(const char *src, const char *dst, const struct stat *info, vmd_stats *stats){
    char buffer[COPY_BUFFER];
    unsigned long long total = 0;
    int failed = 0;
    int in = open(src, O_RDONLY);
    int out;
    ssize_t got;

    if(in < 0){
        copy_error("open", src);
        return -1;
    }
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if(out < 0){
        copy_error("create", dst);
        close(in);
        return -1;
    }
    while((got = read(in, buffer, sizeof(buffer))) > 0){
        ssize_t left = got;
        char *cursor = buffer;

        total += (unsigned long long)got;
        while(left > 0){
            ssize_t wrote = write(out, cursor, (size_t)left);
            if(wrote < 0){
                copy_error("write to", dst);
                failed = 1;
                break;
            }
            if(wrote == 0){
                errno = EIO;
                copy_error("write to", dst);
                failed = 1;
                break;
            }
            cursor += wrote;
            left -= wrote;
        }
        if(failed){
            break;
        }
    }
    if(got < 0){
        copy_error("read", src);
        failed = 1;
    }
    if(!failed && fchmod(out, 0777) != 0){
        copy_error("set permissions on", dst);
        failed = 1;
    }
    if(close(in) != 0){
        copy_error("close", src);
        failed = 1;
    }
    if(close(out) != 0){
        copy_error("close", dst);
        failed = 1;
    }
    if(failed){
        unlink(dst);
        return -1;
    }
    stats->files++;
    stats->bytes += total;
    if(info->st_mtime > stats->newest){
        stats->newest = info->st_mtime;
    }
    return 0;
}

/*
 * Recreate one symbolic link exactly as it was.
 */
static int copy_link(const char *src, const char *dst, vmd_stats *stats){
    char target[PATH_MAX];
    ssize_t length = readlink(src, target, sizeof(target) - 1);

    if(length < 0){
        copy_error("read link", src);
        return -1;
    }
    target[length] = '\0';
    if(symlink(target, dst) != 0){
        copy_error("create link", dst);
        return -1;
    }
    stats->files++;
    stats->bytes += (unsigned long long)length;
    return 0;
}

/*
 * Copy one entry, then walk into directories. This is the only place that
 * touches the file system while saving a version.
 */
static int copy_tree_inner(const char *src, const char *dst, const char *relative,
                           const vmd_ignore *ignore, vmd_stats *stats, int depth){
    struct stat info;

    if(depth > MAX_DEPTH){
        fprintf(stderr, "error: the folder tree is deeper than %d levels: %s\n", MAX_DEPTH, src);
        return -1;
    }
    if(lstat(src, &info) != 0){
        perror(src);
        return -1;
    }
    if(ignore != NULL && relative[0] != '\0' &&
       ignore_match(ignore, relative, S_ISDIR(info.st_mode))){
        stats->skipped++;
        return 0;
    }
    if(S_ISLNK(info.st_mode)){
        return copy_link(src, dst, stats);
    }
    if(S_ISREG(info.st_mode)){
        return copy_file(src, dst, &info, stats);
    }
    if(S_ISDIR(info.st_mode)){
        DIR *dir;
        struct dirent *entry;

        dir = opendir(src);
        if(dir == NULL){
            perror(src);
            return -1;
        }
        if(mkdir(dst, 0700) != 0 && errno != EEXIST){
            perror(dst);
            closedir(dir);
            return -1;
        }
        while((entry = readdir(dir)) != NULL){
            char source[PATH_MAX];
            char target[PATH_MAX];
            char child[PATH_MAX];

            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            if(build_path(source, sizeof(source), "%s/%s", src, entry->d_name) != 0 ||
               build_path(target, sizeof(target), "%s/%s", dst, entry->d_name) != 0){
                fprintf(stderr, "error: path is too long\n");
                closedir(dir);
                return -1;
            }
            if(relative[0] == '\0'){
                snprintf(child, sizeof(child), "%s", entry->d_name);
            }else{
                snprintf(child, sizeof(child), "%s/%s", relative, entry->d_name);
            }
            if(copy_tree_inner(source, target, child, ignore, stats, depth + 1) != 0){
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        if(chmod(dst, 0777) != 0){
            perror(dst);
            return -1;
        }
        return 0;
    }
    fprintf(stderr, "warning: skipping special file %s\n", src);
    stats->skipped++;
    return 0;
}

/*
 * Copy a directory tree without ignore rules (used by export).
 */
int copy_tree(const char *src, const char *dst, vmd_stats *stats){
    struct stat info;

    if(lstat(src, &info) != 0){
        return -1;
    }
    if(S_ISDIR(info.st_mode) == 0){
        errno = ENOTDIR;
        return -1;
    }
    return copy_tree_inner(src, dst, "", NULL, stats, 0);
}

/*
 * Copy a directory tree while honoring its .vmdignore (used by add and up).
 */
int copy_project(const char *src, const char *dst, vmd_stats *stats){
    struct stat info;
    vmd_ignore rules;

    if(lstat(src, &info) != 0){
        return -1;
    }
    if(S_ISDIR(info.st_mode) == 0){
        errno = ENOTDIR;
        return -1;
    }
    if(ignore_load(&rules, src) != 0){
        return -1;
    }
    return copy_tree_inner(src, dst, "", &rules, stats, 0);
}

/*
 * nftw callback that deletes one entry after its children are gone.
 */
static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *walk){
    (void)info;
    (void)type;
    (void)walk;
    return remove(path);
}

/*
 * Make one real directory writable by its owner. Symbolic links are never
 * touched: chmod follows them and would change a target outside the tree.
 * Returns 1 when the mode actually changed, so an unreadable directory can
 * be scanned again without risking a loop.
 */
static int unlock_directory(const char *path){
    struct stat current;
    int fd;
    int changed;

    if(lstat(path, &current) != 0 || S_ISDIR(current.st_mode) == 0){
        return 0;
    }
    changed = (current.st_mode & 0700) != 0700;
    fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if(fd >= 0){
        if(fchmod(fd, 0700) != 0){
            close(fd);
            return 0;
        }
        close(fd);
        return changed;
    }
    if(errno != EACCES && errno != EPERM){
        return 0;
    }
    if(chmod(path, 0700) != 0){
        return 0;
    }
    return changed;
}

/*
 * First nftw pass: give every directory write permission before the delete
 * pass visits its children. It runs without FTW_DEPTH, so parents come
 * first. FTW_DNR means nftw could not read the directory at all; after the
 * chmod it is scanned again so nested unreadable folders are unlocked too.
 */
static int unlock_entry(const char *path, const struct stat *info, int type, struct FTW *walk){
    (void)info;
    (void)walk;
    if(type != FTW_D && type != FTW_DP && type != FTW_DNR){
        return 0;
    }
    if(unlock_directory(path) && type == FTW_DNR){
        (void)nftw(path, unlock_entry, 64, FTW_PHYS);
    }
    return 0;
}

/*
 * Delete a file or a whole directory tree.
 *
 * The first pass repairs directory permissions (a snapshot can contain
 * read-only folders), then the second pass removes everything. Doing the
 * chmod in the FTW_DEPTH handler would be too late: children are visited
 * before their parent, so their entries could not be unlinked.
 */
int remove_tree(const char *path){
    struct stat info;

    if(lstat(path, &info) != 0){
        return errno == ENOENT ? 0 : -1;
    }
    if(S_ISDIR(info.st_mode)){
        (void)nftw(path, unlock_entry, 64, FTW_PHYS);
    }
    return nftw(path, remove_entry, 64, FTW_DEPTH | FTW_PHYS) == 0 ? 0 : -1;
}

/*
 * Count files, bytes, and the newest timestamp in a tree.
 */
int measure_tree(const char *path, vmd_stats *stats){
    struct stat info;

    if(lstat(path, &info) != 0){
        return -1;
    }
    if(S_ISDIR(info.st_mode)){
        DIR *dir = opendir(path);
        struct dirent *entry;

        if(dir == NULL){
            return -1;
        }
        while((entry = readdir(dir)) != NULL){
            char child[PATH_MAX];

            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            if(build_path(child, sizeof(child), "%s/%s", path, entry->d_name) != 0){
                closedir(dir);
                return -1;
            }
            if(measure_tree(child, stats) != 0){
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        return 0;
    }
    if(S_ISREG(info.st_mode)){
        stats->files++;
        stats->bytes += (unsigned long long)info.st_size;
        if(info.st_mtime > stats->newest){
            stats->newest = info.st_mtime;
        }
    }
    return 0;
}

/*
 * Parse a "vN" folder name and return N, or -1.
 */
int version_number(const char *name){
    char *end;
    long value;

    if(name == NULL || name[0] != 'v' || name[1] == '\0'){
        return -1;
    }
    errno = 0;
    value = strtol(name + 1, &end, 10);
    if(errno != 0 || *end != '\0' || value <= 0 || value > 1000000){
        return -1;
    }
    return (int)value;
}

/*
 * Return the next free version number in a project folder, or -1 on error.
 */
int next_version(const char *project){
    DIR *dir = opendir(project);
    struct dirent *entry;
    int last = 0;

    if(dir == NULL){
        return -1;
    }
    while((entry = readdir(dir)) != NULL){
        int version = version_number(entry->d_name);
        if(version > last){
            last = version;
        }
    }
    closedir(dir);
    return last + 1;
}

/*
 * Format a Unix timestamp for display.
 */
void format_time(long long epoch, char *buf, size_t size){
    time_t value = (time_t)epoch;
    struct tm parts;

    if(epoch <= 0 || localtime_r(&value, &parts) == NULL){
        snprintf(buf, size, "-");
        return;
    }
    if(strftime(buf, size, "%Y-%m-%d %H:%M", &parts) == 0){
        snprintf(buf, size, "-");
    }
}

/*
 * Format a byte count with binary units.
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
 * Keep one message on one index line: tabs and newlines become spaces.
 */
void copy_message(char *dst, size_t size, const char *message){
    size_t length = 0;

    if(size == 0){
        return;
    }
    if(message != NULL){
        while(*message == ' ' || *message == '\t'){
            message++;
        }
        while(*message != '\0' && length + 1 < size){
            unsigned char c = (unsigned char)*message++;
            dst[length++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : (char)c;
        }
        while(length > 0 && dst[length - 1] == ' '){
            length--;
        }
    }
    dst[length] = '\0';
}
