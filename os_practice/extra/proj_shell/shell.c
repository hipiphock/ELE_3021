#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BUFFER_SIZE     4096

typedef struct Command {
    char * cmd;
    char ** argv;
    size_t argc;
    size_t argv_size;
    pid_t pid;
} Command;

typedef enum State{
    QUIT = 0,
    RUNNING
} STATE;

typedef enum Mode{
    INTERACTIVE = 1,
    BATCH
} MODE;

typedef Command** CommandVector;

char* get_input(FILE* inputFile);
CommandVector parse_buffer(char* buffer);
Command * parse_command(const char* commandString);
int execute(const CommandVector commandVector);
void free_command_vec(CommandVector commandVector);

int main(int argc, char* argv[]) {

    // for batch / interactive mode
    MODE mode;
    if(argc == INTERACTIVE)
        mode = INTERACTIVE;
    else
        mode = BATCH;

    FILE* input = NULL;
    if (mode == INTERACTIVE)
        input = stdin;
    else if (mode == BATCH)
        input = fopen(argv[1], "r");

    if (input == NULL) {
        fprintf(stderr, "Opening input stream failed.\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        char* buffer;
        CommandVector cmd_vec;

        if (mode == INTERACTIVE) {
            printf("prompt> ");
            fflush(stdout);
        }
        buffer = get_input(input);
        if (buffer == NULL || strcmp(buffer, "quit\n") == 0) {
            break;
        }
        // second 
        cmd_vec = parse_buffer(buffer);
        execute(cmd_vec);
        // when finished
        free(buffer);
        free_command_vec(cmd_vec);
    }
    exit(EXIT_SUCCESS);
}

char* get_input(FILE* inputFile) {
    char* buffer;
    if ((buffer = calloc(BUFFER_SIZE, sizeof(char))) == NULL ) {
        return NULL;
    }
    return fgets(buffer, sizeof(*buffer) * BUFFER_SIZE, inputFile);
}

CommandVector parse_buffer(char* buffer) {
    CommandVector commandVector;
    size_t command_vec_size;
    size_t command_vec_cap;
    Command* command;
    char* p;

    command_vec_size = 0;
    command_vec_cap = 1;
    commandVector = calloc(command_vec_cap + 1, sizeof(*commandVector));
    p = strtok(buffer, ";\n");

    while (p != NULL) {
        command = parse_command(p);
        if (command == NULL) {
            p = strtok(NULL, ";\n");
            continue;
        }
        commandVector[command_vec_size++] = command;
        commandVector[command_vec_size] = NULL;
        // Expand commandVector
        if (command_vec_size == command_vec_cap) {
            command_vec_cap *= 2;
            commandVector = realloc(commandVector, sizeof(*commandVector) * (command_vec_cap + 1));
        }
        p = strtok(NULL, ";\n");
    }
    return commandVector;
}

Command* parse_command(const char* commandString) {
    Command* command_struct;
    const char* tok_start;
    const char* tok_end;

    command_struct = calloc(1, sizeof(Command));
    command_struct->argc = 0;
    command_struct->argv_size = 1;
    command_struct->argv = calloc(command_struct->argv_size + 1, sizeof(*command_struct->argv));

    // copy command
    tok_start = commandString;
    while (isspace(*tok_start) && *tok_start != '\0') {
        tok_start++;
    }
    // commandString is a whitespace string or empty string.
    if (*tok_start == '\0') {
        free(command_struct->argv);
        free(command_struct);
        return NULL;
    }
    tok_end = tok_start;
    while (!isspace(*tok_end) && *tok_end != '\0') {
        tok_end++;
    }
    command_struct->cmd = calloc(tok_end - tok_start + 1, sizeof(char));
    strncpy(command_struct->cmd, tok_start, tok_end - tok_start);

    // copy arguments
    tok_end = commandString;
    while (1) {
        tok_start = tok_end;
        while (isspace(*tok_start) && *tok_start != '\0') {
            tok_start++;
        }
        if (*tok_start == '\0') {
            return command_struct;
        }
        tok_end = tok_start;
        while (!isspace(*tok_end) && *tok_end != '\0') {
            tok_end++;
        }
        int tok_size = tok_end - tok_start;
        command_struct->argv[command_struct->argc] = calloc(tok_end - tok_start + 1, sizeof(char));
        strncpy(command_struct->argv[command_struct->argc], tok_start, tok_end - tok_start);
        command_struct->argv[++command_struct->argc] = NULL;
        // check if it needs to expand argv
        if (command_struct->argc == command_struct->argv_size) {
            command_struct->argv_size *= 2;
            command_struct->argv = realloc(command_struct->argv, sizeof(*command_struct->argv) * (command_struct->argv_size + 1));
        }
    }
}

// execute function
int execute(const CommandVector commandVector) {
    int ret_value;
    int i;

    for (i = 0; commandVector[i] != NULL; i++) {
        Command* command;
        pid_t pid;
        command = commandVector[i];
        pid = fork();
        // case: child process
        if (pid == 0) {
            execvp(command->cmd, command->argv);
            exit(EXIT_SUCCESS);
        } 
        // case: fork error
        else if (pid == -1) {
            fprintf(stderr, "Error: fork failed\n");
            return -1;
        }
        // case: parent process
        else {
            command->pid = pid;
        }
    }
    ret_value = 0;
    while (i-- > 0) {
        int pid;
        int status;
        pid = wait(&status);
        ret_value |= status;
    }
    return ret_value;
}

// function that frees command vector
void free_command_vec(CommandVector commandVector) {
    int i;
    for (i = 0; commandVector[i] != NULL; i++) {
        Command* command = commandVector[i];
        int j;
        for (j = 0; j < command->argc; j++) {
            free(command->argv[j]);
        }
        free(command->argv);
        free(command->cmd);
        free(command);
    }
    free(commandVector);
}
