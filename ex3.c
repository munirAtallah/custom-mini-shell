#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <ctype.h>
#include <pthread.h>

#define MAX_CMD_LENGTH 1024
#define MAX_ARGUMENTS 64

void checkCommand(const char *command);
char** splitToWords(char* command);
void freeWords(char** words);
int compareWithDangerList(const char* inputCommand, char** dangerList, int dangerCount);
void freeDangerList(char** dangerList, int count);
char** loadDangerCommands(const char* filename, int* count);
void printPrompt(int cmdCount, int blockedCount, double lastTime, double totalTime, double minTime, double maxTime);
int handlePipe(char* command);
int handleMyTee(char** args);
int handleRlimit(char** args);
int redirectStderrToFile(char* command);
rlim_t parseResourceValue(const char* str);
char* trimWhitespace(char* str);
int handleMcalc(char** args);

//Main Shell

int main() {
    char input[MAX_CMD_LENGTH];
    int cmdCount = 0, blockedCount = 0, dangerCount = 0;{}
    double totalTime = 0, minTime = 1e9, maxTime = 0, lastTime = 0;
    char** dangerList = loadDangerCommands("danger.txt", &dangerCount);

    while (1) {
        printPrompt(cmdCount, blockedCount, lastTime, totalTime, minTime, maxTime);
        if (!fgets(input, MAX_CMD_LENGTH, stdin)) break;
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) continue;

        // Handle exit commands
        if (strcmp(input, "done") == 0 || strcmp(input, "exit") == 0) {
            break;
        }

        checkCommand(input);
        if (!compareWithDangerList(input, dangerList, dangerCount)) {
            blockedCount++;
            continue;
        }

        // Check for background process
        bool background = false;
        char* inputCopy = strdup(input);
        char* trimmed = trimWhitespace(inputCopy);
        if (strlen(trimmed) > 0 && trimmed[strlen(trimmed)-1] == '&') {
            background = true;
            trimmed[strlen(trimmed)-1] = '\0';
            trimmed = trimWhitespace(trimmed);
        }
        strcpy(input, trimmed);
        free(inputCopy);

        struct timeval start, end;
        gettimeofday(&start, NULL);

        if (strstr(input, "|")) {
            handlePipe(input);
        } else if (strstr(input, "2>")) {
            redirectStderrToFile(input);
        } else {
            char** args = splitToWords(input);
            if (!args || !args[0]) {
                freeWords(args);
                continue;
            }

            if (strcmp(args[0], "my_tee") == 0) {
                handleMyTee(args);
            } else if (strcmp(args[0], "rlimit") == 0) {
                handleRlimit(args);
            } else if (strcmp(args[0], "mcalc") == 0) {
                handleMcalc(args);
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    execvp(args[0], args);
                    perror("execvp");
                    exit(1);
                } else if (pid > 0) {
                    if (!background) {
                        waitpid(pid, NULL, 0);
                    }
                }
            }
            freeWords(args);
        }

        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec)/1e6;
        
        if (!background) {
            lastTime = elapsed;
            totalTime += elapsed;
            if (elapsed < minTime) minTime = elapsed;
            if (elapsed > maxTime) maxTime = elapsed;
            cmdCount++;
        } else {
            cmdCount++;
        }
    }

    freeDangerList(dangerList, dangerCount);
    return 0;
}

char* trimWhitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void checkCommand(const char* command) {
    int count = 0;
    bool inWord = false, err_space = false;
    for (int i = 0; command[i]; i++) {
        if (command[i] == ' ') {
            if (inWord) inWord = false;
            if (i > 0 && command[i - 1] == ' ') err_space = true;
        } else if (!inWord) {
            count++;
            inWord = true;
        }
    }
    if (count > MAX_ARGUMENTS) printf("ERR_ARGS\n");
    if (err_space) printf("ERR_SPACE\n");
}

//Split string to words

char** splitToWords(char* command) {
    char** words = malloc((MAX_ARGUMENTS + 2) * sizeof(char*));
    int count = 0;
    char* p = command;

    while (*p) {
        while (isspace((unsigned char)*p)) p++;

        if (*p == '\0') break;

        char* start;
        if (*p == '"') {
            p++;
            start = p;
            while (*p && *p != '"') p++;
        } else {
            start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
        }

        int len = p - start;
        char* word = malloc(len + 1);
        strncpy(word, start, len);
        word[len] = '\0';
        words[count++] = word;

        if (*p == '"') p++; 
    }

    words[count] = NULL;
    return words;
}

//Free word array

void freeWords(char** words) {
    if (!words) return;
    for (int i = 0; words[i]; i++) free(words[i]);
    free(words);
}

//Danger command checks

int compareWithDangerList(const char* input, char** dangerList, int count) {
    if (!dangerList) return 1;
    
    for (int i = 0; i < count; i++) {
        if (strcmp(input, dangerList[i]) == 0) {
            printf("ERR: Dangerous command detected (\"%s\"). Execution prevented.\n", dangerList[i]);
            return 0;
        }
        char* inCopy = strdup(input);
        char* dangerCopy = strdup(dangerList[i]);
        if (!inCopy || !dangerCopy) {
            free(inCopy); free(dangerCopy);
            continue;
        }
        char* inCmd = strtok(inCopy, " ");
        char* dangCmd = strtok(dangerCopy, " ");
        if (inCmd && dangCmd && strcmp(inCmd, dangCmd) == 0)
            printf("WARNING: Command similar to dangerous command (\"%s\"). Proceed with caution.\n", dangerList[i]);
        free(inCopy); free(dangerCopy);
    }
    return 1;
}

//Load danger commands from file

char** loadDangerCommands(const char* file, int* count) {
    FILE* fp = fopen(file, "r");
    *count = 0;
    if (!fp) return NULL;
    
    char** list = NULL;
    char buf[MAX_CMD_LENGTH];
    while (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\n")] = '\0';
        if (strlen(buf) == 0) continue;
        list = realloc(list, (*count + 1) * sizeof(char*));
        list[*count] = strdup(buf);
        (*count)++;
    }
    fclose(fp);
    return list;
}

//Free danger list memory

void freeDangerList(char** dangerList, int count) {
    if (!dangerList) return;
    for (int i = 0; i < count; i++) free(dangerList[i]);
    free(dangerList);
}

//Prompt printing

void printPrompt(int cmds, int blocked, double last, double total, double min, double max) {
    double avg = cmds ? total / cmds : 0.0;
    printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",
           cmds, blocked, last, avg, min == 1e9 ? 0.0 : min, max);
}

//Simulated `tee` command: writes stdin to multiple files + stdout

int handleMyTee(char** args) {
    int append = 0, start = 1;
    if (args[1] && strcmp(args[1], "-a") == 0) {
        append = 1;
        start = 2;
    }
    
    int fds[MAX_ARGUMENTS], count = 0;
    for (int i = start; args[i]; i++) {
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        fds[count] = open(args[i], flags, 0644);
        if (fds[count] < 0) {
            perror("open");
            for (int j = 0; j < count; j++) close(fds[j]);
            return -1;
        }
        count++;
    }
    
    char buf[1024];
    ssize_t bytes;
    while ((bytes = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, bytes);
        for (int i = 0; i < count; i++) {
            write(fds[i], buf, bytes);
        }
    }
    
    for (int i = 0; i < count; i++) close(fds[i]);
    return 0;
}

//Handles pipe commands: cmd1 | cmd2 

int handlePipe(char* command) {
    char* commandCopy = strdup(command);
    char* cmd1 = strtok(commandCopy, "|");
    char* cmd2 = strtok(NULL, "");
    if (!cmd1 || !cmd2) {
        free(commandCopy);
        return -1;
    }
    
    cmd1 = trimWhitespace(cmd1);
    cmd2 = trimWhitespace(cmd2);
    
    char** args1 = splitToWords(cmd1);
    char** args2 = splitToWords(cmd2);
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        free(commandCopy);
        freeWords(args1);
        freeWords(args2);
        return -1;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(args1[0], args1);
        perror("execvp");
        exit(1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        
        if (strcmp(args2[0], "my_tee") == 0) {
            handleMyTee(args2);
            exit(0);
        } else {
            execvp(args2[0], args2);
            perror("execvp");
            exit(1);
        }
    }

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    
    free(commandCopy);
    freeWords(args1);
    freeWords(args2);
    return 0;
}

int redirectStderrToFile(char* command) {
    char* commandCopy = strdup(command);
    char* cmd = strtok(commandCopy, "2>");
    char* file = strtok(NULL, "");
    if (!cmd || !file) {
        free(commandCopy);
        return -1;
    }
    
    cmd = trimWhitespace(cmd);
    file = trimWhitespace(file);
    
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        free(commandCopy);
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fd, STDERR_FILENO);
        close(fd);
        char** args = splitToWords(cmd);
        execvp(args[0], args);
        perror("execvp");
        exit(1);
    } else if (pid > 0) {
        close(fd);
        waitpid(pid, NULL, 0);
    }
    
    free(commandCopy);
    return 0;
}

// === Parse resource value with units ===
rlim_t parseResourceValue(const char* str) {
    if (strcmp(str, "unlimited") == 0) return RLIM_INFINITY;
    
    char* endptr;
    long value = strtol(str, &endptr, 10);
    
    if (*endptr != '\0') {
        switch (*endptr) {
            case 'K': case 'k': value *= 1024; break;
            case 'M': case 'm': value *= 1024 * 1024; break;
            case 'G': case 'g': value *= 1024 * 1024 * 1024; break;
            case 'B': case 'b': break;
        }
    }
    
    return (rlim_t)value;
}

//Resource limit handler (show and set)

int handleRlimit(char** args) {
    if (!args[1]) return -1;

    if (strcmp(args[1], "show") == 0) {
        struct rlimit r;
        
        if (args[2] && strcmp(args[2], "cpu") == 0) {
            getrlimit(RLIMIT_CPU, &r);
            printf("CPU time limits: soft=%s, hard=%s\n", 
                   r.rlim_cur == RLIM_INFINITY ? "unlimited" : "limited",
                   r.rlim_max == RLIM_INFINITY ? "unlimited" : "limited");
            return 0;
        }
        
        // Show all limits
        getrlimit(RLIMIT_CPU, &r);
        if (r.rlim_cur == RLIM_INFINITY) {
            printf("CPU time: soft=unlimited, hard=unlimited\n");
        } else {
            printf("CPU time: soft=%llus, hard=%llus\n", (unsigned long long)r.rlim_cur, (unsigned long long)r.rlim_max);
        }
        
        getrlimit(RLIMIT_AS, &r);
        printf("Memory: soft=%ld, hard=%ld\n", 
               r.rlim_cur == RLIM_INFINITY ? -1L : (long)r.rlim_cur,
               r.rlim_max == RLIM_INFINITY ? -1L : (long)r.rlim_max);
        
        getrlimit(RLIMIT_FSIZE, &r);
        printf("File size: soft=%ld, hard=%ld\n", 
               r.rlim_cur == RLIM_INFINITY ? -1L : (long)r.rlim_cur,
               r.rlim_max == RLIM_INFINITY ? -1L : (long)r.rlim_max);
        
        getrlimit(RLIMIT_NOFILE, &r);
        printf("Open files: soft=%llu, hard=%llu\n", (unsigned long long)r.rlim_cur, (unsigned long long)r.rlim_max);
        
        return 0;
    }

    if (strcmp(args[1], "set") == 0) {
        struct rlimit cpu = {RLIM_INFINITY, RLIM_INFINITY};
        struct rlimit mem = {RLIM_INFINITY, RLIM_INFINITY};
        struct rlimit fsize = {RLIM_INFINITY, RLIM_INFINITY};
        struct rlimit nofile = {RLIM_INFINITY, RLIM_INFINITY};
        struct rlimit nproc = {RLIM_INFINITY, RLIM_INFINITY};
        int i = 2;

        while (args[i] && strchr(args[i], '=')) {
            char* argCopy = strdup(args[i]);
            char* eq = strchr(argCopy, '=');
            if (!eq) {
                free(argCopy);
                i++;
                continue;
            }
            
            *eq = '\0';
            char* key = argCopy;
            char* val = eq + 1;
            
            rlim_t cur = 0, max = 0;
            if (strchr(val, ':')) {
                char* valCopy = strdup(val);
                char* colon = strchr(valCopy, ':');
                *colon = '\0';
                cur = parseResourceValue(valCopy);
                max = parseResourceValue(colon + 1);
                free(valCopy);
            } else {
                cur = max = parseResourceValue(val);
            }
            
            if (strcmp(key, "cpu") == 0) cpu = (struct rlimit){cur, max};
            else if (strcmp(key, "mem") == 0) mem = (struct rlimit){cur, max};
            else if (strcmp(key, "fsize") == 0) fsize = (struct rlimit){cur, max};
            else if (strcmp(key, "nofile") == 0) nofile = (struct rlimit){cur, max};
            else if (strcmp(key, "nproc") == 0) nproc = (struct rlimit){cur, max};
            
            free(argCopy);
            i++;
        }

        if (!args[i]) {
            fprintf(stderr, "No command specified after resource limits\n");
            return -1;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            setrlimit(RLIMIT_CPU, &cpu);
            setrlimit(RLIMIT_AS, &mem);
            setrlimit(RLIMIT_FSIZE, &fsize);
            setrlimit(RLIMIT_NOFILE, &nofile);
            setrlimit(RLIMIT_NPROC, &nproc);
            
            execvp(args[i], &args[i]);
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFSIGNALED(status)) {
                int sig = WTERMSIG(status);
                if (sig == SIGXCPU) {
                    printf("CPU time limit exceeded!\n");
                } else if (sig == SIGKILL) {
                    printf("Memory allocation failed!\n");
                } else if (sig == SIGSEGV) {
                    printf("Memory allocation failed!\n");
                } else if (sig == SIGXFSZ) {
                    printf("File size limit exceeded!\n");
                } else {
                    printf("Terminated by signal: %s\n", strsignal(sig));
                }
            } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                // Command failed normally, don't print additional error
            }
        } else {
            perror("fork");
            return -1;
        }
        return 0;
    }
    return -1;
}

typedef struct {
    int size;      
    double* data;      
} Matrix;

typedef struct {
    Matrix* mat1;
    Matrix* mat2;
    Matrix* result;
    char operation;    
} ThreadData;

typedef struct {
    Matrix** matrices;
    int count;
    char operation;
    Matrix* result;
} ComputeData;

Matrix* parseMatrix(const char* str) {
    if (!str || str[0] != '(' || str[strlen(str)-1] != ')') {
        return NULL;
    }
    
    char* copy = strdup(str + 1); 
    copy[strlen(copy) - 1] = '\0';
    
    char* comma = strchr(copy, ',');
    if (!comma) {
        free(copy);
        return NULL;
    }
    
    // Parse rows
    char rowStr[32];
    int commaPos = comma - copy;
    strncpy(rowStr, copy, commaPos);
    rowStr[commaPos] = '\0';
    int rows = atoi(rowStr);
    
    char* colon = strchr(copy, ':');
    if (!colon) {
        free(copy);
        return NULL;
    }
    
    char colStr[32];
    int colonPos = colon - copy;
    int colStart = commaPos + 1;
    int colLen = colonPos - colStart;
    strncpy(colStr, copy + colStart, colLen);
    colStr[colLen] = '\0';
    int cols = atoi(colStr);
    
    if (rows != cols || rows <= 0) {
        free(copy);
        return NULL;
    }
    
    Matrix* matrix = malloc(sizeof(Matrix));
    matrix->size = rows;
    matrix->data = malloc(rows * cols * sizeof(double));
    
    char* dataStr = colon + 1;
    char* dataCopy = strdup(dataStr);
    char* token = strtok(dataCopy, ",");
    int index = 0;
    
    while (token && index < rows * cols) {
        matrix->data[index++] = atof(token);
        token = strtok(NULL, ",");
    }
    
    free(dataCopy);
    
    if (index != rows * cols) {
        free(matrix->data);
        free(matrix);
        free(copy);
        return NULL;
    }
    
    free(copy);
    return matrix;
}

Matrix* copyMatrix(Matrix* src) {
    if (!src) return NULL;
    
    Matrix* copy = malloc(sizeof(Matrix));
    copy->size = src->size;
    copy->data = malloc(src->size * src->size * sizeof(double));
    memcpy(copy->data, src->data, src->size * src->size * sizeof(double));
    return copy;
}

void freeMatrix(Matrix* matrix) {
    if (matrix) {
        free(matrix->data);
        free(matrix);
    }
}

void* matrixOperation(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    if (!data->mat1 || !data->mat2 || data->mat1->size != data->mat2->size) {
        return NULL;
    }
    
    int size = data->mat1->size;
    data->result = malloc(sizeof(Matrix));
    data->result->size = size;
    data->result->data = malloc(size * size * sizeof(double));
    
    for (int i = 0; i < size * size; i++) {
        if (data->operation == 'A') {
            data->result->data[i] = data->mat1->data[i] + data->mat2->data[i];
        } else if (data->operation == 'S') {
            data->result->data[i] = data->mat1->data[i] - data->mat2->data[i];
        }
    }
    
    return NULL;
}

// Recursive parallel computation
Matrix* computeParallel(Matrix** matrices, int count, char operation) {
    if (count == 1) {
        return copyMatrix(matrices[0]);
    }
    
    if (count == 2) {
        ThreadData data = {matrices[0], matrices[1], NULL, operation};
        pthread_t thread;
        pthread_create(&thread, NULL, matrixOperation, &data);
        pthread_join(thread, NULL);
        return data.result;
    }
    
    int pairs = count / 2;
    int remaining = count % 2;
    
    pthread_t* threads = malloc(pairs * sizeof(pthread_t));
    ThreadData* threadData = malloc(pairs * sizeof(ThreadData));
    Matrix** nextLevel = malloc((pairs + remaining) * sizeof(Matrix*));
    
    for (int i = 0; i < pairs; i++) {
        threadData[i].mat1 = matrices[i * 2];
        threadData[i].mat2 = matrices[i * 2 + 1];
        threadData[i].result = NULL;
        threadData[i].operation = operation;
        pthread_create(&threads[i], NULL, matrixOperation, &threadData[i]);
    }
    
    for (int i = 0; i < pairs; i++) {
        pthread_join(threads[i], NULL);
        nextLevel[i] = threadData[i].result;
    }
    
    if (remaining) {
        nextLevel[pairs] = copyMatrix(matrices[count - 1]);
    }
    
    Matrix* result = computeParallel(nextLevel, pairs + remaining, operation);
    
    for (int i = 0; i < pairs; i++) {
        freeMatrix(nextLevel[i]);
    }
    if (remaining && pairs + remaining > 1) {
        freeMatrix(nextLevel[pairs]);
    }
    
    free(threads);
    free(threadData);
    free(nextLevel);
    
    return result;
}

void printMatrix(Matrix* matrix) {
    printf("(%d,%d:", matrix->size, matrix->size);
    for (int i = 0; i < matrix->size * matrix->size; i++) {
        if (i > 0) printf(",");
        if (matrix->data[i] == (int)matrix->data[i]) {
            printf("%d", (int)matrix->data[i]);
        } else {
            printf("%.10g", matrix->data[i]);
        }
    }
    printf(")\n");
}

int handleMcalc(char** args) {
    if (!args[1]) {
        printf("ERR_MAT_INPUT\n");
        return -1;
    }
    
    int argCount = 0;
    while (args[argCount + 1]) argCount++;
    
    if (argCount < 3) {
        printf("ERR_MAT_INPUT\n");
        return -1;
    }
    
    char* operation = args[argCount];
    if (strcmp(operation, "ADD") != 0 && strcmp(operation, "SUB") != 0) {
        printf("ERR_MAT_INPUT\n");
        return -1;
    }

    int matrixCount = argCount - 1;
    Matrix** matrices = malloc(matrixCount * sizeof(Matrix*));
    
    for (int i = 0; i < matrixCount; i++) {
        matrices[i] = parseMatrix(args[i + 1]);
        if (!matrices[i]) {
            for (int j = 0; j < i; j++) {
                freeMatrix(matrices[j]);
            }

            free(matrices);
            printf("ERR_MAT_INPUT\n");
            return -1;
        }

        if (i > 0 && matrices[i]->size != matrices[0]->size) {
            for (int j = 0; j <= i; j++) {
                freeMatrix(matrices[j]);
            }

            free(matrices);
            printf("ERR_MAT_INPUT\n");
            return -1;
        }
    }
    
    char op = (operation[0] == 'A') ? 'A' : 'S';
    Matrix* result = computeParallel(matrices, matrixCount, op);
    
    if (result) {
        printMatrix(result);
        freeMatrix(result);
    } else {
        printf("ERR_MAT_INPUT\n");
    }
    
    for (int i = 0; i < matrixCount; i++) {
        freeMatrix(matrices[i]);
    }
    free(matrices);
    
    return 0;
}