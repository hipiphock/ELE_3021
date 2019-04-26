#include "types.h"
#include "user.h"
#include "stat.h"

int main(int argc, char *argv[])
{
    int pid = fork();
while(1){
    if (pid < 0){
        // case: error
        printf(1, "error");
        exit();
    }
    else if (pid == 0){
            // case: child process
            printf(1, "Child\n");
            yield();
    }
    else{
            // case: parent
            printf(1, "Parent\n");
            yield();
    }
}
    exit();
}
