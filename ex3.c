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

// === Main Shell Loop ===
int main() {
    char input[MAX_CMD_LENGTH];
    int cmdCount = 0, blockedCount = 0, dangerCount = 0;
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

        // Only update timing stats for non-background processes
        if (!background) {
            lastTime = elapsed;
            totalTime += elapsed;
            if (elapsed < minTime) minTime = elapsed;
            if (elapsed > maxTime) maxTime = elapsed;
            cmdCount++;
        } else {
            // For background processes, don't wait but still count the command
            cmdCount++;
        }
    }

    freeDangerList(dangerList, dangerCount);
    return 0;
}

// === Utility: Trim whitespace ===
char* trimWhitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// === Utility: Validate command ===
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

// === Utility: Split string to words ===
char** splitToWords(char* command) {
    char** words = malloc((MAX_ARGUMENTS + 2) * sizeof(char*));
    int count = 0;
    char* commandCopy = strdup(command);
    char* token = strtok(commandCopy, " \t");
    while (token && count < MAX_ARGUMENTS + 1) {
        words[count++] = strdup(token);
        token = strtok(NULL, " \t");
    }
    words[count] = NULL;
    free(commandCopy);
    return words;
}

// === Utility: Free word array ===
void freeWords(char** words) {
    if (!words) return;
    for (int i = 0; words[i]; i++) free(words[i]);
    free(words);
}

// === Danger command filtering ===
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

// === Load danger commands from file ===
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

// === Free danger list memory ===
void freeDangerList(char** dangerList, int count) {
    if (!dangerList) return;
    for (int i = 0; i < count; i++) free(dangerList[i]);
    free(dangerList);
}

// === Prompt printing with stats ===
void printPrompt(int cmds, int blocked, double last, double total, double min, double max) {
    double avg = cmds ? total / cmds : 0.0;
    printf("#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>>",
           cmds, blocked, last, avg, min == 1e9 ? 0.0 : min, max);
}

// === Simulated `tee` command: writes stdin to multiple files + stdout ===
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

// === Handles pipe commands: cmd1 | cmd2 ===
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
        // First command - write to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(args1[0], args1);
        perror("execvp");
        exit(1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Second command - read from pipe
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (strcmp(args2[0], "my_tee") == 0) {
            handleMyTee(args2);
            exit(0);  // Critical fix: exit after handleMyTee to avoid execvp
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

// === Redirect stderr to a file: command 2> file.txt ===
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
            case 'B': case 'b': break; // bytes, no multiplication
        }
    }

    return (rlim_t)value;
}

// === Resource limit handler (show and set) ===
int handleRlimit(char** args) {
    if (!args[1]) return -1;

    if (strcmp(args[1], "show") == 0) {
        struct rlimit r;

        // Check if specific resource requested
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

        // Parse resource limits
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
            // Child process - set limits and execute command
            // Set limits silently, only report critical failures
            setrlimit(RLIMIT_CPU, &cpu);
            setrlimit(RLIMIT_AS, &mem);
            setrlimit(RLIMIT_FSIZE, &fsize);
            setrlimit(RLIMIT_NOFILE, &nofile);
            // NPROC often fails due to permissions, set it but don't report error
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
