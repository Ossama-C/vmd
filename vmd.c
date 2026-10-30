#include <stdio.h>
#include <string.h>

#include "vmd.h"

/*
 * Return 1 when two strings are identical.
 */
static int same(const char *left, const char *right){
    return strcmp(left, right) == 0;
}

/*
 * Print a usage error and a hint to run "vmd help".
 */
static int usage_error(const char *command){
    if(command == NULL){
        fprintf(stderr, "error: something is missing\n");
    }else{
        fprintf(stderr, "error: wrong arguments for \"%s\"\n", command);
    }
    fprintf(stderr, "run \"vmd help\" to see the commands\n");
    return 1;
}

/*
 * Read the command line and call the matching command.
 */
int main(int argc, char *argv[]){
    const char *command;

    if(argc < 2){
        return usage_error(NULL);
    }
    command = argv[1];

    if(same(command, "help") || same(command, "-h") || same(command, "--help")){
        if(argc != 2){
            return usage_error(command);
        }
        return help();
    }
    if(same(command, "version") || same(command, "-v") || same(command, "--version")){
        if(argc != 2){
            return usage_error(command);
        }
        printf("vmd %s\n", VMD_VERSION);
        return 0;
    }

    if(same(command, "add")){
        if(argc != 3 && argc != 4){
            return usage_error(command);
        }
        return add(argv[2], argc == 4 ? argv[3] : "");
    }

    if(same(command, "up")){
        if(argc != 3 && argc != 4){
            return usage_error(command);
        }
        return up(argv[2], argc == 4 ? argv[3] : "");
    }

    if(same(command, "imp") || same(command, "export")){
        int version;

        if(argc != 5){
            return usage_error(command);
        }
        version = parse_version(argv[3]);
        if(version <= 0){
            fprintf(stderr, "error: the version must be a positive number\n");
            return 1;
        }
        return imp(argv[2], version, argv[4]);
    }

    if(same(command, "list")){
        if(argc != 2 && argc != 3){
            return usage_error(command);
        }
        return list(argc, argv);
    }

    if(same(command, "info") || same(command, "log")){
        return info(argc, argv);
    }

    if(same(command, "find") || same(command, "search")){
        return find(argc, argv);
    }

    if(same(command, "rm") || same(command, "remove")){
        const char *positional[2];
        int count = 0;
        int assumed_yes = 0;

        for(int i = 2; i < argc; i++){
            if(same(argv[i], "-y") || same(argv[i], "--yes")){
                assumed_yes = 1;
            }else if(count < 2){
                positional[count++] = argv[i];
            }else{
                return usage_error(command);
            }
        }
        if(count < 1){
            return usage_error(command);
        }
        return rm_cmd(count, positional, assumed_yes);
    }

    fprintf(stderr, "error: unknown command \"%s\"\n", command);
    fprintf(stderr, "run \"vmd help\" to see the commands\n");
    return 1;
}
