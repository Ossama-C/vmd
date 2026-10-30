#ifndef VMD_H
#define VMD_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define VMD_VERSION "2.0.0"
#define VMD_NAME_MAX 64
#define VMD_MSG_MAX 200
#define VMD_PATTERN_MAX 256
#define VMD_PATTERNS 64

/* One line of ~/vmd/index.vmd. */
typedef struct {
    char project[VMD_NAME_MAX];
    int version;
    long long epoch;
    unsigned long files;
    unsigned long long bytes;
    char message[VMD_MSG_MAX];
} vmd_entry;

/* The whole index, held in memory while a command runs. */
typedef struct {
    vmd_entry *items;
    int count;
    int capacity;
} vmd_index;

/* Numbers collected while copying or measuring a folder. */
typedef struct {
    unsigned long files;
    unsigned long long bytes;
    unsigned long skipped;
    time_t newest;
} vmd_stats;

/* one function per command, called from vmd.c */
int add(const char *name, const char *message);
int up(const char *name, const char *message);
int imp(const char *name, int version, const char *destination);
int list(int argc, char **argv);
int info(int argc, char **argv);
int find(int argc, char **argv);
int rm_cmd(int count, const char *const *args, int assumed_yes);
int help(void);

/* util.c: names, paths, copying, deleting, formatting */
int valid_name(const char *name);
int parse_version(const char *text);
const char *vmd_home(void);
int build_path(char *buf, size_t size, const char *fmt, ...);
int mkdir_p(const char *path, mode_t mode);
int is_inside(const char *path, const char *dir);
int copy_tree(const char *src, const char *dst, vmd_stats *stats);
int copy_project(const char *src, const char *dst, vmd_stats *stats);
int remove_tree(const char *path);
int measure_tree(const char *path, vmd_stats *stats);
int version_number(const char *name);
int next_version(const char *project);
void format_time(long long epoch, char *buf, size_t size);
void human_size(unsigned long long bytes, char *buf, size_t size);
void copy_message(char *dst, size_t size, const char *message);

/* index.c: the central index and the rebuild command */
int index_path(char *buf, size_t size);
int index_load(vmd_index *index);
int index_save(vmd_index *index);
void index_free(vmd_index *index);
int index_find(const vmd_index *index, const char *project, int version);
int index_count(const vmd_index *index, const char *project);
int index_last_version(const vmd_index *index, const char *project);
long long index_last_epoch(const vmd_index *index, const char *project);
int meta_record(const char *project, int version, const char *message, const vmd_stats *stats);
int meta_forget(const char *project, int version);
int meta_clear(void);
int meta_rebuild(void);

#endif
