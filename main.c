#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <glob.h>
#include <sys/wait.h>

/*
A command struct to wrap a command
*/
typedef struct {
    char *commandName;
    char **arguments;
    int argumentLength;
} command;

typedef struct job {
    pid_t *allPids;
    int pidCount;
    char *inputCommand;
} job;

void printJobs();
void fgCommand(int);

/*
Split string with a separator, return a array of string.
*/
char **tokensFromString(char *inputString, char *separator, int *outputLength){
    
    char *tok = NULL;
    char inputCopy[256];

    // becoz strtok like to fu-k with the input string we have to copy it first.
    strcpy(inputCopy, inputString);

    tok = strtok(inputCopy, separator);

    int length = 0;
    while (tok) {
        tok = strtok(NULL, separator);
        length++;
    }

    char **tokens = NULL;
    tokens = malloc(sizeof(char*)*length);
    tok = strtok(inputString, separator);
    int i = 0;
    while (tok) {
        tokens[i] = malloc(sizeof(tok));
        tokens[i] = tok;
        tok = strtok(NULL, separator);
        i++;
    }
    *outputLength = length;

    return tokens;
}

/*
Turn a line of input into commands, it handles pipe as well.
*/
command *commandsFromString(char *inputString, int *outputLength) {

    int length = 0;
    char **commandStrings = tokensFromString(inputString, "|", &length);
    command *results = malloc(sizeof(command)*length);
    int i = 0;
    
    for (i = 0; i < length; i++) {
        char *thisCommandString = commandStrings[i];
        int commandPartsLength = 0;
        int j = 0;
        char **tokens = tokensFromString(thisCommandString, " ", &commandPartsLength);
        // printf("length %d\n", commandPartsLength);

        results[i].commandName = malloc(sizeof(char*));
        results[i].arguments = malloc(sizeof(char*)*commandPartsLength-1);
        results[i].argumentLength = commandPartsLength-1;

        for (j = 0; j < commandPartsLength; j++) {
            char *thistoken = tokens[j];

            if (j == 0) {
                results[i].commandName = thistoken;
                // printf("Token \"%s\" (Command)\n", thistoken);
            } else {
                results[i].arguments[j-1] = thistoken;
                // printf("Token \"%s\" (Arguments)\n", thistoken);
            }
        }
    }
    *outputLength = i;

    return results;
}

bool startsWith(const char *pre, const char *str) {
    return strncmp(pre, str, strlen(pre)) == 0;
}

/*
搵executable黎run
*/
char* findExecutablePath(char* commandName) {
    if (startsWith("/",commandName) ||
        startsWith("./",commandName) ||
        startsWith("../",commandName)) {
        // absolute path so return the same command name
        return commandName;
    }

    char* absolutePath = malloc(sizeof(char)*256);

    strcpy(absolutePath, "/bin/");
    strcat(absolutePath, commandName);
    if( access( absolutePath, X_OK ) != -1 ) {
        // file exists
        return absolutePath;
    }

    strcpy(absolutePath, "/usr/bin/");
    strcat(absolutePath, commandName);
    if( access( absolutePath, X_OK ) != -1 ) {
        // file exists
        return absolutePath;
    }

    strcpy(absolutePath, "./");
    strcat(absolutePath, commandName);
    if( access( absolutePath, X_OK ) != -1 ) {
        // file exists
        return absolutePath;
    }
    return NULL;
}

static char *currentPath;

char** wildcardExpression(command inputCommand){
    glob_t globbuf; /* For wildcard 開variable */
    globbuf.gl_offs = 1; /* For wildcard reserve globbuf.gl_pathv[0]*/
    int i;

    /* 開始處理wildcard */
    /* 讀argument  */
	if(inputCommand.argumentLength == 1){
        if (glob(inputCommand.arguments[0], GLOB_DOOFFS | GLOB_NOCHECK , NULL , &globbuf))
        {perror("Error");} // 唔知放咩error
    }

    if(inputCommand.argumentLength > 1){
        if (glob(inputCommand.arguments[0], GLOB_DOOFFS | GLOB_NOCHECK , NULL , &globbuf))
        {perror("Error");} 
        for(i = 1;i < inputCommand.argumentLength;i++){
            if (glob(inputCommand.arguments[i], GLOB_DOOFFS | GLOB_NOCHECK | GLOB_APPEND, NULL , &globbuf))
            {perror("Error");}
        }
    }

    return globbuf.gl_pathv;
}

/*
This should execute the command.
*/
pid_t executeCommand(command inputCommand, int prevPipe[2], int nextPipe[2]){
	int i;

    // I don't know why but according to the spec,
    // 'cd' checks number of argument before printing anything,
    // and stop things going when it is not correct.
    if (strcmp(inputCommand.commandName,"cd") == 0) {
        if (inputCommand.argumentLength != 1) {
            printf("cd: wrong number of arguments\n");
            return 0;
        }
    }

    char commandNameKeyword[] = {'\t','>','<','|','*','!','`','\'','\"'};
    for (i = 0; i < 9; i++) {
        if (strchr(inputCommand.commandName,commandNameKeyword[i]) != NULL) {
            printf("Error: invalid input command line\n");
            return 0;
        }
    }

    char argumentKeyword[] = {'\t','>','<','|','!','`','\'','\"'};
    for (i = 0; i < 8; i++){
        int j = 0;
        for (j = 0; j < inputCommand.argumentLength; j++) {
            if (strchr(inputCommand.arguments[j], argumentKeyword[i]) != NULL) {
                printf("Error: invalid input command line (Arg)\n");
                return 0;
            }
        }
    }

    if (strcmp(inputCommand.commandName,"cd") == 0) {
        // 如果係cd嘅話
        if (chdir(inputCommand.arguments[0]) == -1) {
            printf("%s: cannot change directory\n", inputCommand.arguments[0]);
        } else {
            currentPath = getcwd(NULL,0);
        }

    } else if (strcmp(inputCommand.commandName,"exit") == 0) {
        // 如果係exit嘅話
        printf("[ Shell Terminated ]\n");
        exit(0);

    } else if (strcmp(inputCommand.commandName,"fg") == 0) {
        if (inputCommand.argumentLength != 1) {
            printf("fg: wrong number of arguments\n");
            return 0;
        }

        fgCommand(atoi(inputCommand.arguments[0]));

    } else if (strcmp(inputCommand.commandName,"jobs") == 0) {
        printJobs();
    } else {
        // 唔係build in command即係直接執行啦
        inputCommand.commandName = findExecutablePath(inputCommand.commandName);

        char **path = wildcardExpression(inputCommand);

        // make an array for execvp
        char **commandToExecute = malloc(sizeof(char*)*inputCommand.argumentLength+2); // +1 for the command name
        
        if(inputCommand.argumentLength > 0){
            path[0] = inputCommand.commandName;
            commandToExecute[0] = inputCommand.commandName;
        } else {
            commandToExecute[0] = inputCommand.commandName; //When not using GLOB_DOOFFS before, the program will crash , so will not push into globbuf when arg = 0
            commandToExecute[1] = NULL;
        }

        pid_t childPid = fork();
        if (childPid == 0) {
            // child process

            // 0 stands for read
            // 1 stands for write
            
            if (nextPipe != NULL) { 
                dup2(nextPipe[1],1); // write to the next pipe
                close(nextPipe[0]); // 無叉用
            }

            if (prevPipe != NULL) {
                dup2(prevPipe[0],0); // read the previous pipe
                close(prevPipe[1]); // the child does not need this end of the pipe
            }

            if(inputCommand.argumentLength>0){
                execvp(path[0], &path[0]);
            } else {
                execvp(commandToExecute[0], &commandToExecute[0]);
            }

            /* If execvp returns, it must have failed. */
            perror(commandToExecute[0]);

            printf("Unknown command\n");
            exit(0);
        } else {
            return childPid;
        }
    }
    return 0;
}

void SIGHANDLER(int SIG){ // Signal Handler
	return;
}

static job *backgroundJobs = NULL;
static int backgroundJobsCount = 0;
static job *currentJob = NULL;

void suspendCurrentJob(int SIG){
    if (currentJob == NULL) {
        printf("No job to suspend\n");
        return;
    }
    job *_backgroundJobs = backgroundJobs;
    backgroundJobs = malloc(sizeof(job)*(backgroundJobsCount+1));
    int i;
    for (i = 0; i < backgroundJobsCount; i++){
        backgroundJobs[i] = _backgroundJobs[i];
    }
    backgroundJobs[i] = *currentJob;
    // free(_backgroundJobs);
    backgroundJobsCount++;

    printf("\nsuspended [%d] %s\n", i, currentJob->inputCommand);

    for (i = 0; i < currentJob->pidCount; i++) {
        pid_t currentPid = currentJob->allPids[i];
        // int result = kill(currentPid, SIGTSTP);
        // printf("stopping %d, result=%d\n", currentPid, result);
    }
}

void printJobs(){
    int i;
    for (i = 0; i < backgroundJobsCount; i++) {
        job thisJob = backgroundJobs[i];
        printf("[%d] %s\n", i, thisJob.inputCommand);
    }
}

void continueJob(job thisJob) {
    int i;
    for (i = 0; i <thisJob.pidCount; i++) {
        pid_t thisPid = thisJob.allPids[i];
        kill(thisPid, SIGCONT);
    }
    *currentJob = thisJob;
}

void fgCommand(int jobNumber){
    if(jobNumber >= backgroundJobsCount || jobNumber < 0){
        printf("fg: no such job\n");
        return;
   }
    job thisJob = backgroundJobs[jobNumber];

    job *_backgroundJobs = backgroundJobs;
    backgroundJobs = malloc(sizeof(job)*(backgroundJobsCount-1));
    int i;
    int newIndex = 0;
    for (i = 0; i < backgroundJobsCount; i++){
        if (i != jobNumber) {
            backgroundJobs[newIndex] = _backgroundJobs[i];
            newIndex++;
        }
    }
    backgroundJobsCount--;
    printf("Job Wake Up: %s\n",thisJob.inputCommand);
    continueJob(thisJob);
    int status;
    for (i = 0; i < thisJob.pidCount; i++) {
        waitpid(-1, &status, WUNTRACED);
    }
}

int main(int argc, char const *argv[]){
    
	signal(SIGINT,SIGHANDLER); /*  (Ctrl + C) */
	signal(SIGQUIT,SIGHANDLER); /*  default signal of command “kill” */
	signal(SIGTERM,SIGHANDLER); /*  (Ctrl + \) */
	signal(SIGTSTP,suspendCurrentJob); /*  (Ctrl + Z) */

    char *inputString = malloc(sizeof(char)*256);
    currentPath = getcwd(NULL,0);
    while (1) {
        printf("[3150 shell:%s]$ ", currentPath);
        if (fgets(inputString, 256, stdin) == NULL) {
            printf("[ Shell Terminated ]\n");
            exit(0);
        }
        if (strcmp(inputString,"\n")==0) {
            // fu*k the empty line
            continue;
        } else {
            strtok(inputString, "\n");
        }
        int commandLength = 0;
		int i;
        int needWaitCount = 0;

        // 個過程大概就係：
        // 1. 開新process
        // 2. 如果有上一個command嘅file descriptor，就叫個新process去含佢條pipe
        // 3. 如果有下一個command，即係後面有pipe嘅話，就整個file descriptor，俾下一個command含

        // 呢個係最新嘅process嘅file descriptor，即係如果有新process嘅話，佢係會變嘅
        int *currentFileDescriptor = NULL;
        currentJob = malloc(sizeof(job));
        currentJob->inputCommand = malloc(sizeof(char)*strlen(inputString));
        strcpy(currentJob->inputCommand, inputString);

        command *allCommends = commandsFromString(inputString, &commandLength);

        currentJob->allPids = malloc(sizeof(pid_t)*commandLength);
        currentJob->pidCount = commandLength;

        for (i = 0; i < commandLength; i++) {

            // 呢個係我地要屌嘅command
            command currentCommand = allCommends[i];

            int *thisFileDescriptor = NULL;
            if (i < commandLength-1) {
                // 唔係最後一個command，即係之後有野，所以整定舊野比下個用
                thisFileDescriptor = malloc(sizeof(int[2]));
                pipe(thisFileDescriptor);
            }
            // GOGOGO!
            pid_t thisPid = executeCommand( currentCommand, currentFileDescriptor, thisFileDescriptor );
            if (currentFileDescriptor) {
                // 因爲呢個command已經出發左，所以可以close左上個command嘅pipe
                close(currentFileDescriptor[0]);
                close(currentFileDescriptor[1]);
            }
            if (thisFileDescriptor != NULL) {
                // 然後留比下個command用
                currentFileDescriptor = thisFileDescriptor;
            }

            if (thisPid != 0) {
                // 唔係build in野，係堅野
                currentJob->allPids[i] = thisPid;
                needWaitCount++;
            }
        }

        // 執行左幾多個command，就等幾多次
        for (i = 0; i < needWaitCount; i++) {
            int status;
            waitpid(-1, &status, WUNTRACED);
        }

        // free(currentJob);
        currentJob = NULL;
    }
}

char* HongKongPolice(){
    return "thugs";
}