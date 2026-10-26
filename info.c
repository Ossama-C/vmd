#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "info.h"
#include "meta.h"
#include "util.h"

/*
 * qsort comparator that orders entries by project name, then version.
 */
static int compare_entries(const void *left, const void *right){
    const vmd_entry *a = *(const vmd_entry *const *)left;
    const vmd_entry *b = *(const vmd_entry *const *)right;
    int by_project = strcmp(a->project, b->project);

    if(by_project != 0){
        return by_project;
    }
    return (a->version > b->version) - (a->version < b->version);
}

/*
 * Return 1 when needle occurs in text, ignoring ASCII case.
 */
static int contains_ci(const char *text, const char *needle){
    size_t needle_length = strlen(needle);

    if(needle_length == 0){
        return 1;
    }
    for(; *text != '\0'; text++){
        size_t i = 0;
        while(i < needle_length && text[i] != '\0' &&
              tolower((unsigned char)text[i]) == tolower((unsigned char)needle[i])){
            i++;
        }
        if(i == needle_length){
            return 1;
        }
    }
    return 0;
}

/*
 * Print the header row shared by the info version tables.
 */
static void print_header(void){
    printf("%-10s %-16s %7s %11s  %s\n", "VERSION", "DATE", "FILES", "SIZE", "MESSAGE");
}

/*
 * Print one indexed version with a formatted date, size, and message.
 */
static void print_entry(const vmd_entry *entry, const char *version_name){
    char date[32];
    char size[32];

    format_time(entry->epoch, date, sizeof(date));
    human_size(entry->bytes, size, sizeof(size));
    printf("%-10s %-16s %7lu %11s  %s\n",
           version_name, date, entry->files, size,
           entry->message[0] != '\0' ? entry->message : "-");
}

/*
 * Print the index path, project/version counts, and newest timestamp.
 */
static int summary(void){
    char path[PATH_MAX];
    vmd_index index;
    long long last_epoch = 0;
    size_t projects = 0;

    if(index_path(path, sizeof(path)) != 0){
        fprintf(stderr, "error: HOME is not set\n");
        return 1;
    }
    if(index_load(&index) != 0){
        fprintf(stderr, "error: could not read the index\n");
        return 1;
    }

    for(size_t i = 0; i < index.count; i++){
        int seen = 0;
        for(size_t j = 0; j < i; j++){
            if(strcmp(index.items[i].project, index.items[j].project) == 0){
                seen = 1;
                break;
            }
        }
        if(seen == 0){
            projects++;
        }
        if(index.items[i].epoch > last_epoch){
            last_epoch = index.items[i].epoch;
        }
    }

    char date[32];
    format_time(last_epoch, date, sizeof(date));
    printf("index:      %s\n", path);
    printf("projects:   %zu\n", projects);
    printf("versions:   %zu\n", index.count);
    printf("last write: %s\n\n", date);
    printf("use \"vmd info <project>\" or \"vmd find <text>\"\n");

    index_free(&index);
    return 0;
}

/*
 * Run "vmd info" and "vmd info --rebuild"; lookups read only the index.
 */
int info(int argc, char **argv){
    const char *name;
    int version = 0;
    vmd_index index;
    const vmd_entry **matches;
    size_t match_count = 0;

    if(argc == 2){
        return summary();
    }
    if(argc == 3 && strcmp(argv[2], "--rebuild") == 0){
        return meta_rebuild();
    }
    if(argc < 3 || argc > 4){
        fprintf(stderr, "error: wrong arguments for \"info\"\n");
        fprintf(stderr, "usage: vmd info [project] [version]\n");
        return 1;
    }

    name = argv[2];
    if(valid_name(name) == 0){
        fprintf(stderr, "error: invalid project name\n");
        return 1;
    }
    if(argc == 4){
        version = parse_version(argv[3]);
        if(version <= 0){
            fprintf(stderr, "error: the version must be a positive number\n");
            return 1;
        }
    }
    if(index_load(&index) != 0){
        fprintf(stderr, "error: could not read the index\n");
        return 1;
    }

    matches = malloc((index.count == 0 ? 1 : index.count) * sizeof(*matches));
    if(matches == NULL){
        index_free(&index);
        return 1;
    }
    for(size_t i = 0; i < index.count; i++){
        if(strcmp(index.items[i].project, name) != 0){
            continue;
        }
        if(version != 0 && index.items[i].version != version){
            continue;
        }
        matches[match_count++] = &index.items[i];
    }

    if(match_count == 0){
        if(version != 0){
            printf("no metadata for \"%s\" v%d\n", name, version);
        }else{
            printf("no metadata for \"%s\"\n", name);
        }
        printf("if the version exists on disk, run \"vmd info --rebuild\"\n");
        free(matches);
        index_free(&index);
        return 1;
    }
    qsort(matches, match_count, sizeof(*matches), compare_entries);

    printf("project: %s\n", name);
    if(version != 0){
        printf("version: v%d\n", version);
    }else{
        printf("versions: %zu\n", match_count);
    }
    printf("\n");
    print_header();
    for(size_t i = 0; i < match_count; i++){
        char version_name[16];
        snprintf(version_name, sizeof(version_name), "v%d", matches[i]->version);
        print_entry(matches[i], version_name);
    }

    free(matches);
    index_free(&index);
    return 0;
}

/*
 * Search the index for text in project names, version names, and messages.
 */
int find(int argc, char **argv){
    vmd_index index;
    const vmd_entry **matches;
    size_t match_count = 0;

    if(argc != 3){
        fprintf(stderr, "error: wrong arguments for \"find\"\n");
        fprintf(stderr, "usage: vmd find <text>\n");
        return 1;
    }
    if(argv[2][0] == '\0'){
        fprintf(stderr, "error: the search text is empty\n");
        return 1;
    }
    if(index_load(&index) != 0){
        fprintf(stderr, "error: could not read the index\n");
        return 1;
    }
    matches = malloc((index.count == 0 ? 1 : index.count) * sizeof(*matches));
    if(matches == NULL){
        index_free(&index);
        return 1;
    }

    for(size_t i = 0; i < index.count; i++){
        char version_name[16];
        snprintf(version_name, sizeof(version_name), "v%d", index.items[i].version);
        if(contains_ci(index.items[i].project, argv[2]) ||
           contains_ci(index.items[i].message, argv[2]) ||
           contains_ci(version_name, argv[2])){
            matches[match_count++] = &index.items[i];
        }
    }

    if(match_count == 0){
        printf("no matches for \"%s\"\n", argv[2]);
        free(matches);
        index_free(&index);
        return 0;
    }
    qsort(matches, match_count, sizeof(*matches), compare_entries);

    printf("%-24s %-10s %-16s  %s\n", "PROJECT", "VERSION", "DATE", "MESSAGE");
    for(size_t i = 0; i < match_count; i++){
        char date[32];
        char version_name[16];
        format_time(matches[i]->epoch, date, sizeof(date));
        snprintf(version_name, sizeof(version_name), "v%d", matches[i]->version);
        printf("%-24s %-10s %-16s  %s\n",
               matches[i]->project, version_name, date,
               matches[i]->message[0] != '\0' ? matches[i]->message : "-");
    }

    free(matches);
    index_free(&index);
    return 0;
}
