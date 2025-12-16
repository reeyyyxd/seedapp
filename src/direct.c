#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>

int main(){

    DIR* curr_dir = opendir(".");
    struct dirent* dir = NULL;
    
    while((dir = readdir(curr_dir))!=NULL){
        printf("%s\n",dir->d_name);
        // dir = readdir(curr_dir);

    }
    //opendir the long way
    //   DIR* curr_dir = opendir(".");

    // if(curr_dir == NULL){
    //     printf("Not found");
    //     return 1;
    // }

    // struct dirent* dir = readdir(curr_dir);
    
    // while(readdir != NULL){
    //     printf("%s\n",dir->d_name);
    //     dir = readdir(curr_dir);

    // }

   
    return 0;
}