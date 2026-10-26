#ifndef VMD_META_H
#define VMD_META_H

#include <stddef.h>

#include "util.h"

/*
 * One version entry as stored in the central index.
 */
typedef struct {
    char project[VMD_NAME_MAX];
    int version;
    long long epoch;
    unsigned long files;
    unsigned long long bytes;
    char message[VMD_MSG_MAX];
} vmd_entry;

/*
 * In-memory form of the complete central index.
 */
typedef struct {
    vmd_entry *items;
    size_t count;
    size_t capacity;
} vmd_index;

/* Build the path of the central index file, ~/vmd/index.vmd. */
int index_path(char *buf, size_t size);

/* Load and validate the index file; a missing file yields an empty index. */
int index_load(vmd_index *index);

/* Write the index atomically with an fsync and rename. */
int index_save(vmd_index *index);

/* Free the in-memory index and reset its counters. */
void index_free(vmd_index *index);

/* Find one project/version entry and return its position, or -1. */
int index_find(const vmd_index *index, const char *project, int version);

/* Count the indexed versions of a project. */
int index_count(const vmd_index *index, const char *project);

/* Return the highest indexed version number of a project. */
int index_last_version(const vmd_index *index, const char *project);

/* Return the newest indexed timestamp of a project. */
long long index_last_epoch(const vmd_index *index, const char *project);

/* Record or update one version in the index. */
int meta_record(const char *project, int version, const char *message, const copy_stats *stats);

/* Remove one version (or all versions when version is 0) from the index. */
int meta_forget(const char *project, int version);

/* Replace the index with an empty one. */
int meta_clear(void);

/* Rebuild the index by scanning the saved project folders. */
int meta_rebuild(void);

#endif
