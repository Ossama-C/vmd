#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "all.h"
#include "rm.h"

int main(int argc, char *argv[]){

    if(argc == 1){
      printf("something is missing\n");
      printf("us vmd help\n");
      return 1;
    }

    if(strcmp(argv[1], "add") == 0){

       if(argc != 3){
        printf("invalede command us \nvmd help\n");
        return 1;
      }
      int out = add(argv);
       if(out == 0){
        printf("add successful\n");
       }else{
        printf("add failed\n");
        return 1;
       }

    }else if(strcmp(argv[1], "up") == 0){

      if(argc != 3){
        printf("invalede command us \nvmd help\n");
        return 1;
      }
      int out = up(argv);
       if(out == 0){
        printf("update successful\n");
       }else{
        printf("update failed\n");
        return 1;
       }

    }else if(strcmp(argv[1], "imp") == 0){
      if(argc != 5){
        printf("invalede command us \nvmd help\n");
        return 1;
      }      
       int out = imp(argv);
       if(out == 0){
        printf("import successful\n");
       }else{
        printf("import failed\n");
        return 1;
       }

    }else if(strcmp(argv[1], "list") == 0){
 
      if(argc != 2 && argc != 3){
        printf("invalede command us \nvmd help\n");
        return 1;
      }
      int out = list(argc, argv);
       if(out != 0){
        printf("list failed\n");
        return 1;
       }
 
    }else if(strcmp(argv[1], "rm") == 0){

      if(argc != 3 && argc != 4){
        printf("invalede command us \nvmd help\n");
        return 1;
      }
      int out = rm(argc, argv);
       if(out == 0){
        printf("remove successful\n");
       }else{
        printf("remove failed\n");
        return 1;
       }

    }else if(strcmp(argv[1], "help") == 0){

       if(argc != 2){
        printf("invalede command us \nvmd help\n");
        return 1;
    }
      help();

    }else{

        printf("this command not exacte\nto see list of command us\n vmd help\n");
        return 1;
    }
    
    return 0;
}