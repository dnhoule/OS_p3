

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct process
{
    struct process *next; /* next process in pipeline */
    char **argv;          /* for exec */
    int argc;
    pid_t pid;      /* process ID */
    char completed; /* true if process has completed */
    char stopped;   /* true if process has stopped */
    int status;     /* reported status value */
} process;

typedef struct job
{
    int id;
    process *first_process; /* list of processes in this job */
    pid_t pgid;             /* process group ID */

} job;

job *foreground;
struct job *jobs[256];
int numJobs = 0;
pid_t shell_pgid;

job *find_job(pid_t pgid)
{

    for (int i = 0; i < numJobs; i++)
    {
        if (jobs[i]->pgid == pgid)
            return jobs[i];
    }

    return NULL;
}

int job_is_completed(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed)
            return 0;
    return 1;
}

int job_is_stopped(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed && !p->stopped)
            return 0;
    return 1;
}
int storeJob(process p)
{
    for (int i = 0; i < 256; ++i)
    {
        if (jobs[i] == NULL)
        {

            jobs[i] = malloc(sizeof(job));
            jobs[i]->id = i;
            jobs[i]->first_process = &p;
            jobs[i]->pgid = getpgid(p.pid); // Set the process group ID

            // Make sure to update the job list count (numJobs) if you use it
            numJobs++;

            return 0;
        }
    }
    return -1;
}
void waitForJob(int pgid)
{
    int status;
    // printf("BEFORERERER\n");
    printf("Before %d\n", tcgetpgrp(STDIN_FILENO));
    pid_t terminated_pid = waitpid(-1, &status, WUNTRACED);
    // printf("AFTERRERERE\n");

    if (terminated_pid == -1)
    {
        // printf("Error, process not found\n");
    }
    else
    {
        if (status == 5247)
        {
            // storeJob(terminated_pid);
            printf("Terminated by SIGSTP\n");
        }
        // The process with process ID terminated_pid has finished
        printf("Process with PID %d has finished.\n", terminated_pid);
    }
}
void removeTerminatedJobs(void)
{
    int status;

    for (int i = 0; i < numJobs; i++)
    {
        if (waitpid(-(jobs[i]->pgid), &status, WNOHANG) > 0)
        {
            printf("Child job %d has terminated\n", jobs[i]->pgid);

            free(jobs[i]);
            jobs[i] = NULL;

            numJobs--;
        }
    }
}
void sigchld_handler(int signo)
{
    // printf("SIGCHLD CAUGHT\n");
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}
void sigttou_handler(int signo)
{
    // printf("SIGTTOU CAUGHT\n");
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}
void sigstp_handler(int signo)
{
    // printf("HANDLER: Foreground pgid: %d\n", tcgetpgrp(STDIN_FILENO));
    // printf("HANDLER: Shell pgid: %d\n", shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    // printf("HANDLER: Caught SIGTSTP. Shell has regained control.\n");
}
void sigint_handler(int signo)
{
    // printf("HANDLER: Foreground pgid: %d\n", tcgetpgrp(STDIN_FILENO));
    // printf("HANDLER: Shell pgid: %d\n", shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    // printf("HANDLER: Caught SIGINT. Shell has regained control.\n");
}

int fg(int pgid)
{

    tcsetpgrp(STDIN_FILENO, pgid);

    // printf("Foreground Process Before: %d\n", tcgetpgrp(STDIN_FILENO));
    // printf("Shells Process: %d\n", shell_pgid);

    waitForJob(pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    // printf("Foreground Process After: %d\n", tcgetpgrp(STDIN_FILENO));

    // printf("WHATTTT\n");

    return 0;
}

int bg(int id, int stopped)
{
    printf("PGID: %d\n", id);
    if (stopped)
    {
        printf("Process %d continued\n", id);
        kill(id, SIGCONT);
    }
    printf("Process %d running in background\n", id);
    return 0;
}

int runBuiltInCommands(process p)
{

    if (strcmp(p.argv[0], "exit") == 0)
    {
        if (p.argc != 1)
        {
            printf("Error, exit should not have arguments");
            exit(1);
        }
        exit(0);
    }
    else if (strcmp(p.argv[0], "cd") == 0)
    {
        if (p.argc != 2)
        {
            printf("Error, cd should have one argument");
            exit(1);
        }
        else if (chdir(p.argv[1]))
        {
            printf("Could not change to this directory: %s\n", p.argv[1]);
            exit(1);
        }
    }
    else if (strcmp(p.argv[0], "jobs") == 0)
    {
        if (p.argc != 1)
        {
            printf("Error, jobs should not have arguments");
            exit(1);
        }
    }
    else if (strcmp(p.argv[0], "fg") == 0)
    {
        if (p.argc != 1 && p.argc != 2)
        {
            printf("Error, cd should have one argument");
            exit(1);
        }
        if (p.argc == 2)
        {
            int id = atoi(p.argv[1]);
            fg(id);
        }
        // int isFg = strcmp(command, "fg") == 0;
    }
    else if (strcmp(p.argv[0], "bg") == 0)
    {
        int id;
        if (p.argc != 1 && p.argc != 2)
        {
            printf("Error, bg should have at most one argument");
            exit(1);
        }
        if (p.argc == 2)
        {
            id = atoi(p.argv[1]);
            bg(id, 1);
        }
        else
        {
            for (int i = 255; i >= 0; --i)
            {
                if (jobs[i] != NULL && jobs[i]->first_process->stopped)
                {
                    bg(jobs[i]->pgid, 1);
                }
            }
        }
    }
    else
    {
        return -1;
    }
    return 0;
}

int run(process p)
{

    int childId;
    int toBackground = 0;

    if (strcmp(p.argv[p.argc - 1], "&") == 0)
    {
        toBackground = 1;
        strcpy(p.argv[p.argc - 1], "\0");
    }
    if ((childId = fork()) == 0)
    {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        setpgid(childId, childId);

        if (!toBackground)
        {
            tcsetpgrp(STDIN_FILENO, getpgid(childId));
        }
        if (execvp(p.argv[0], p.argv) == -1)
        {
            printf("execvp failed\n");
            exit(1);
        }
        printf("This should not have been reached\n");
        exit(1);
    }
    else if (childId < 0)
    {
        // Fork failed
        printf("Fork failed\n");
        exit(1);
    }
    else
    {
        setpgid(childId, childId);
        p.pid = childId;
        // Parent process
        if (!toBackground)
        {
            // Foreground the process

            fg(getpgid(childId));
        }
        else
        {
            storeJob(p);
            // printf("Childs pgrp: %d Shells pgrp: %d\n", getpgid(childId), getpgid(shell_pgid));
        }
    }
    return 1;
}

int main(int argc, char const *argv[])
{
    if (argc > 2)
    {
        printf("Too many arguments\n");
        exit(1);
    }
    signal(SIGTTOU, SIG_IGN);
    pid_t shell_pid = getpid();
    if (setpgid(shell_pid, shell_pid) < 0)
    {
        perror("Couldn't put the shell in its own process group");
        exit(1);
    }
    /* Grab control of the terminal. */

    signal(SIGQUIT, SIG_IGN);

    signal(SIGTTIN, SIG_IGN);

    signal(SIGCHLD, SIG_IGN);

    shell_pgid = getpgid(shell_pid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    signal(SIGTTOU, sigttou_handler);

    char *line = NULL;

    // Handler for SIGTSTP

    signal(SIGTSTP, sigstp_handler);
    signal(SIGINT, sigint_handler);

    // Set up a signal handler for SIGCHLD
    // signal(SIGCHLD, sigchld_handler);

    printf("OG SHELL PGID: %d\n", shell_pid);

    int read_from_file = 0;
    if (argc == 2)
    {
        read_from_file = 1;
    }

    FILE *input = NULL;

    if (read_from_file)
    {
        input = fopen(argv[1], "r"); /* should check the result */
        if (input == NULL)
        {
            printf("Could not open file\n");
            exit(1);
        }
    }
    else
    {
        printf("wsh> ");
        input = stdin;
    }
    char *delim = " ";
    char *curr = NULL;
    int i;
    char *commandArgs[100];
    size_t size = 256;
    int num = 0;
    while ((num = getline(&line, &size, input)) != -1)
    {
        if (num == 1)
        {
            printf("wsh> ");
            continue;
        }

        if (line[strlen(line) - 1] == '\n')
        {
            line[strlen(line) - 1] = '\0';
        }

        i = 0;
        while ((curr = strsep(&line, delim)) != NULL)
        {
            commandArgs[i] = curr;
            ++i;
        }

        int firstProcess = 1;

        job *j = malloc(sizeof(job));

        j->id = -1;   // Assign -1 for now, not in job list
        j->pgid = -1; /* process group ID */

        int processIndex = 0;
        process *p = malloc(sizeof(process));
        p->argv = malloc(sizeof(char *) * 10);
        process *next;

        for (int k = 0; k < i; ++k)
        {
            // If encountered "|", end process
            if (strcmp(commandArgs[k], "|") == 0)
            {
                p->argc = processIndex; // Num of arguments in command
                p->completed = 0;       // Assuming not completed (0) initially
                p->stopped = 0;         // Assuming not stopped (0) initially
                p->status = 0;          // Status value
                p->pid = -1;            // Assign -1 for now

                next = malloc(sizeof(process));
                p->next = next;

                p = p->next;
                p->argv = malloc(sizeof(char *) * 10);
                processIndex = 0;
            }
            if (k == i - 1)
            {
                printf("YOOOO");
                p->argc = i;      // Num of arguments in command
                p->completed = 0; // Assuming not completed (0) initially
                p->stopped = 0;   // Assuming not stopped (0) initially
                p->status = 0;    // Status value
                p->pid = -1;      // Assign -1 for now
                p->next = NULL;
            }
            p->argv[processIndex] = malloc(sizeof(char) * 10);
            strcpy(p->argv[processIndex], commandArgs[k]);
            printf("%s\n", p->argv[processIndex]);
            processIndex++;

            // Assign values to its members
            if (firstProcess)
            {
                j->first_process = p;
                firstProcess = 0;
            }
        }

        printf("DEBUGGING, FIRST PROCESS IN JOB INFO: \nargv[0]: %s\nargc: %d\n", j->first_process->argv[0], j->first_process->argc);

        foreground = j;
        // Check if it's a built in command

        if (!runBuiltInCommands(*(j->first_process)))
        {
        }
        // If not built in command, try running executable from PATH
        else if (run(*(j->first_process)))
        {
        }
        memset(commandArgs, 0, sizeof(commandArgs));

        // Remove any background jobs that have terminated
        removeTerminatedJobs();

        // printf("NUMJOBS: %d\n", numJobs);
        if (!read_from_file)
        {
            printf("wsh> ");
        }
    }
    free(line);

    exit(0);
}
