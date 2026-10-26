#include <stdio.h>

#include "help.h"
#include "util.h"

/*
 * Print the command reference, storage layout, and ignore rules.
 */
int help(void){
    printf("vmd %s - small snapshot version control\n\n", VMD_VERSION);
    printf("usage:\n");
    printf("  vmd add  <project> [message]        save the current folder as v1\n");
    printf("  vmd up   <project> [message]        save the current folder as the next version\n");
    printf("  vmd imp  <project> <version> <dir>  export a version to <dir> (alias: export)\n");
    printf("  vmd list [project]                  list projects, or the versions of one project\n");
    printf("  vmd info [project] [version]        read dates and details from the index (alias: log)\n");
    printf("  vmd info --rebuild                  rebuild the index from the saved folders\n");
    printf("  vmd find <text>                     search projects, versions and messages (alias: search)\n");
    printf("  vmd rm   <project> [version|all]    remove a project, or only one version\n");
    printf("  vmd rm   all                        remove every saved project\n");
    printf("  vmd help                            show this help\n");
    printf("  vmd version                         show the version of vmd\n");

    printf("\nhow it works:\n");
    printf("  add   run it inside the folder of your project. The folder is copied to v1.\n");
    printf("  up    run it inside the folder of your project. The next number is v2, v3, ...\n");
    printf("  imp   copies a saved version to <dir>/vmd-<project>_v-<version>.\n");
    printf("        An export with the same name is replaced as a whole.\n");
    printf("  rm    without a version it removes the project and all its versions.\n");
    printf("        with a version it removes only that version (1, 2, 3 ...).\n");
    printf("        with \"all\" after a project name it removes the project and all versions.\n");
    printf("        \"vmd rm all\" removes every saved project.\n");
    printf("  list  without a name it shows all projects with their last update date.\n");
    printf("        with a name it shows the versions with dates, sizes and messages.\n");
    printf("  info  reads only the index file, so it is fast and does not scan the projects.\n");
    printf("  find  searches the index only: project names, version numbers and messages.\n");

    printf("\nwhere things are stored:\n");
    printf("  versions  ~/vmd/projects/vmd-<project>/v<version>/\n");
    printf("  index     ~/vmd/index.vmd\n");
    printf("            one line per version: project, version, timestamp, files, bytes, message.\n");
    printf("            It is updated by add/up/rm. Use \"vmd info --rebuild\" if it is lost.\n");

    printf("\nnotes:\n");
    printf("  a project name uses letters, numbers, '-' and '_' only (1-%d characters).\n",
           VMD_NAME_MAX - 1);
    printf("  \"all\" is reserved for \"vmd rm all\".\n");
    printf("  messages are optional and are stored in the index, not inside your project.\n\n");
    printf("ignoring files:\n");
    printf("  put a .vmdignore file in the folder you save. One pattern per line, # for comments.\n");
    printf("  examples: .git   *.o   node_modules/   src/generated\n");
    printf("  a pattern without '/' matches that name at any depth; with '/' it is relative to\n");
    printf("  the folder; a trailing '/' matches directories only. Ignored entries are counted\n");
    printf("  as \"skipped\" and are not copied. Export (imp) always copies the saved version as-is.\n\n");
    return 0;
}
