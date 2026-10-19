#include <stdio.h>
#include "help.h"

int help(){

    printf("commands list\n\n");
    printf("vmd |______________________________________________________________________________________|\n");
    printf("    |     add | \"name of project\"                                                          |\n");
    printf("    |_________|____________________________________________________________________________|\n");
    printf("    |      up | \"name of project\"                                                          |\n");
    printf("    |_________|____________________________________________________________________________|\n");
    printf("    |     imp | \"name of project\" number \"path of the folder you want to import\"           |\n");
    printf("    |_________|____________________________________________________________________________|\n");
    printf("    |      rm | \"name of project\" [version|all]                                           |\n");
    printf("    |      rm | all                                                                         |\n");
    printf("    |_________|____________________________________________________________________________|\n");
    printf("    |    list | [\"name of project\"]                                                        |\n");
    printf("    |_________|____________________________________________________________________________|\n");

    printf("\nhow to use:\n");
    printf("  add   run it inside the folder of your project.\n");
    printf("        It saves the folder as version 1.\n");
    printf("  up    run it inside the folder of your project.\n");
    printf("        It saves the current state as the next version (2, 3, 4 ...).\n");
    printf("  imp   number is the version you want (1, 2, 3 ...).\n");
    printf("        The version is copied to a new folder named vmd-<project>_v-<number>\n");
    printf("        inside the path you give. If that folder already exists,\n");
    printf("        files with the same names are overwritten.\n");
    printf("  rm    without a version removes the project and all its versions.\n");
    printf("        with a version it removes only that version (1, 2, 3 ...).\n");
    printf("        with 'all' after a project name it removes all its versions.\n");
    printf("        'vmd rm all' removes every saved project.\n");
    printf("  list  without a name it shows all your projects.\n");
    printf("        with a name it shows the versions of that project.\n");

    printf("\nnotes:\n");
    printf("  a project name uses letters, numbers, '-' and '_' only (less than 30 characters).\n");
    printf("  everything is saved in ~/vmd/projects.\n\n");

return 0;
}