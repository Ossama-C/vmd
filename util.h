#ifndef VMD_UTIL_H
#define VMD_UTIL_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define VMD_VERSION "1.1.0"
#define VMD_NAME_MAX 64
#define VMD_MSG_MAX 200

/*
 * Counters collected while copying or measuring a project tree.
 */
typedef struct {
    unsigned long files;
    unsigned long long bytes;
    unsigned long skipped;
    time_t newest;
} copy_stats;

/* Check that a project name is valid and not the reserved word "all". */
int valid_name(const char *name);

/* Parse a positive version number. */
int parse_version(const char *text);

/* Return the HOME directory, or NULL when it is not set. */
const char *vmd_home(void);

/* Format a path and fail instead of truncating it. */
int build_path(char *buf, size_t size, const char *fmt, ...);

/* Create a directory and all of its missing parents. */
int mkdir_p(const char *path, mode_t mode);

/* Resolve a path and normalize the part that does not exist yet. */
int resolve_path(const char *path, char *buf, size_t size);

/* Return 1 when path is equal to dir or lies below it. */
int is_inside(const char *path, const char *dir);

/* Copy a directory tree without ignore rules. */
int copy_tree(const char *src, const char *dst, copy_stats *stats);

/* Copy a project tree using its .vmdignore file. */
int copy_project(const char *src, const char *dst, copy_stats *stats);

/* Delete a file or directory tree. */
int remove_tree(const char *path);

/* Count files, bytes, and the newest timestamp in a tree. */
int measure_tree(const char *path, copy_stats *stats);

/* Parse a "vN" directory name and return N, or -1. */
int version_number(const char *name);

/* Return the next free version number in a project directory. */
int next_version(const char *project_path);

/* Format a Unix timestamp for display. */
void format_time(long long epoch, char *buf, size_t size);

/* Format a byte count with binary units. */
void human_size(unsigned long long bytes, char *buf, size_t size);

/* Sanitize and truncate a message for one index line. */
void copy_message(char *dst, size_t size, const char *message);

#endif
