

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct process
{
    struct process *next;
    char **argv;
    int argc;
    pid_t pid;
    char completed;
    char stopped;
    int status;
} process;

typedef struct job
{
    int id;
    process *first_process;
    pid_t pgid;
    int hasAmpersand; // Initially run in background
    int status;       // 0 if running, 1 if stopped
    int stdin;
    int stdout;

} job;

int interactive = 1;
struct job *jobs[256];
int numJobs = 0;
pid_t shell_pgid = 0;
job *foreground;

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

void sigint_handler(int signo)
{

    // printf("HANDLER: Foreground pgid: %d\n", tcgetpgrp(STDIN_FILENO));
    // printf("HANDLER: Shell pgid: %d\n", shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    // printf("HANDLER: Caught SIGINT. Shell has regained control.\n");
}

job *find_job(pid_t pgid)
{
    // If ID not specified, find job with highest pgid
    if (pgid == -1)
    {

        for (int i = 255; i >= 0; i--)
        {

            if (jobs[i] != NULL)
            {
                printf("Should not have hit index %d\n", i);
                return jobs[i];
            }
        }
    }
    // Else, search for match
    else
    {

        for (int i = 0; i < 256; i++)
        {
            if (jobs[i] != NULL && jobs[i]->pgid == pgid)
                return jobs[i];
        }
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
int storeJob(job *j)
{
    for (int i = 0; i < 256; ++i)
    {
        if (jobs[i] == NULL)
        {
            j->id = i;
            jobs[i] = j;

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
    // printf("Before %d\n", tcgetpgrp(STDIN_FILENO));
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
            printf("Terminated by SIGSTP\n");
            foreground->status = 1;
            storeJob(foreground);
        }
        // The process with process ID terminated_pid has finished
        // printf("Process with PID %d has finished.\n", terminated_pid);
    }
}
void removeTerminatedJobs(void)
{
    int status;
    int result;
    process *p;
    for (int i = 0; i < 256; i++)
    {
        if (jobs[i] != NULL)
        {
            int allProcessesTerminated = 1;
            for (p = jobs[i]->first_process; p; p = p->next)
            {
                if (!p->completed)
                {
                    result = waitpid(p->pid, &status, WNOHANG);
                    if (result == -1)
                    {
                        perror("waitpid");
                        break;
                    }
                    if (result == 0)
                    {
                        allProcessesTerminated = 0;
                        break;
                    }
                    if (WIFEXITED(status) || WIFSIGNALED(status))
                    {
                        p->completed = 1;
                    }
                }
            }
            if (allProcessesTerminated)
            {
                printf("All processes in job %d have terminated\n", jobs[i]->pgid);
                jobs[i] = NULL;

                numJobs--;
            }
        }
    }
}

void display_jobs()
{
    process *p;
    for (int i = 0; i < 256; ++i)
    {

        if (jobs[i] != NULL)
        {
            printf("%d: ", jobs[i]->id);

            for (p = jobs[i]->first_process; p; p = p->next)
            {
                for (int j = 0; j < p->argc; ++j)
                {

                    printf("%s ", p->argv[j]);
                }
                if (p->next != NULL)
                {
                    printf("| ");
                }
            }
            if (jobs[i]->hasAmpersand)
            {
                printf("&");
            }
            printf("\n");
        }
    }
}

int fg(job *j, int stopped)
{

    tcsetpgrp(STDIN_FILENO, j->pgid);

    // printf("Foreground Process Before: %d\n", tcgetpgrp(STDIN_FILENO));
    // printf("Shells Process: %d\n", shell_pgid);

    // If stopped in background, continue it in foreground
    if (stopped)
    {
        kill(-j->pgid, SIGCONT);
    }

    waitForJob(j->pgid);

    // Now that foreground finished, put shell back in foreground
    tcsetpgrp(STDIN_FILENO, shell_pgid);

    // printf("Foreground Process After: %d\n", tcgetpgrp(STDIN_FILENO));

    // printf("WHATTTT\n");

    return 0;
}

int bg(job *j, int stopped)
{
    if (j == NULL)
    {
        printf("Error, not a valid job\n");
        return 1;
    }
    // printf("PGID: %d\n", id);
    if (stopped)
    {
        // printf("Process %d continued in background\n", j->pgid);

        if (kill(-j->pgid, SIGCONT) < 0)
        {
            printf("Could not continue process %d\n", j->pgid);
            return 0;
        }
        j->status = 0;
    }
    printf("Process %d continued in background\n", j->pgid);
    // printf("Process %d running in background\n", id);

    return 0;
}

void sigstp_handler(int signo)
{

    // // If there was a job in foreground, send it to background
    // if (foreground != NULL)
    // {
    //     foreground->status = 1; // Set the running process to a stopped status
    //     storeJob(foreground);   // Add this process to the jobs list
    //     foreground = NULL;
    // }

    if (tcgetpgrp(STDIN_FILENO) != shell_pgid)
    {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }
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
        display_jobs();
    }
    else if (strcmp(p.argv[0], "fg") == 0)
    {
        int pgid;
        job *j;
        if (p.argc != 1 && p.argc != 2)
        {
            printf("Error, fg should have at most one argument");
            exit(1);
        }
        else if (p.argc == 2)
        {

            pgid = atoi(p.argv[1]);

            j = find_job(pgid);

            if (j == NULL)
            {
                printf("Error, could not find job with pgid %d\n", pgid);
                return 0;
            }

            fg(j, 0);
        }
        else
        {
            // No pgid provided, find the largest
            j = find_job(-1);

            if (j == NULL)
            {
                printf("Error, no job available\n");
                return 0;
            }

            fg(j, 1);
        }
    }
    else if (strcmp(p.argv[0], "bg") == 0)
    {
        int pgid;
        job *j;
        if (p.argc != 1 && p.argc != 2)
        {
            printf("Error, bg should have at most one argument");
            exit(1);
        }
        else if (p.argc == 2)
        {
            pgid = atoi(p.argv[1]);

            j = find_job(pgid);

            if (j == NULL)
            {
                printf("Error, could not find job with pgid %d\n", pgid);
                return 0;
            }

            bg(j, 1);
            storeJob(j);
        }
        else
        {
            // No pgid provided, find the largest
            j = find_job(-1);

            if (j == NULL)
            {
                printf("Error, no job available\n");
                return 0;
            }

            bg(j, 1);
        }
    }
    else
    {
        return -1;
    }
    return 0;
}

int runExec(process *p, job *j, int infile, int outfile)
{

    int childId;

    if ((childId = fork()) == 0)
    {

        signal(SIGINT, sigint_handler);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        // signal(SIGTSTP, sigstp_handler);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        if (j->pgid == 0)
        {
            j->pgid = childId;
        }

        setpgid(childId, j->pgid);

        if (!j->hasAmpersand)
        {
            tcsetpgrp(STDIN_FILENO, getpgid(childId));
        }
        // printf("Infile: %d, Outfile: %d\n", infile, outfile);
        if (infile != STDIN_FILENO)
        {
            dup2(infile, STDIN_FILENO);
            close(infile);
        }

        if (outfile != STDOUT_FILENO)
        {
            // printf("Infile: %d, Outfile: %d\n", infile, outfile);
            dup2(outfile, STDOUT_FILENO);
            // printf("BRUHHHHH\n");
            close(outfile);
        }

        // printf("EXECING %s\n", p->argv[0]);
        if (execvp(p->argv[0], p->argv) == -1)
        {
            printf("execvp failed\n");
            foreground = NULL;
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
        if (!j->pgid)
        {
            j->pgid = childId;
        }

        setpgid(childId, j->pgid);
    }
    return 1;
}

void runJob(job *j)
{
    process *p;
    int pipe_io[2];
    int infile;
    int outfile;

    infile = j->stdin;
    for (p = j->first_process; p; p = p->next)
    {
        // set outfile to be input of next process
        if (p->next)
        {
            if (pipe(pipe_io) < 0)
            {
                perror("pipe");
                exit(1);
            }
            outfile = pipe_io[1];
        }
        else
            outfile = j->stdout;
        // printf("argv[0]: %s\nargc: %d\n", p->argv[0], p->argc);
        if (!runBuiltInCommands((*p)))
        {
        }
        // If not built in command, try running executable from PATH
        else if (runExec(p, j, infile, outfile))
        {
        }

        if (infile != j->stdin)
            close(infile);
        if (outfile != j->stdout)
            close(outfile);
        infile = pipe_io[0];
    }
    if (!j->hasAmpersand)
        fg(j, 0);
    else
    {
        bg(j, 1);
        storeJob(j);
    }
}
int main(int argc, char const *argv[])
{
    if (argc > 2)
    {
        printf("Too many arguments\n");
        exit(1);
    }

    if (argc == 2)
    {

        interactive = 0;
    }

    signal(SIGTTOU, SIG_IGN);

    pid_t shell_pid = getpid();

    // // shell_pgid = getpgid(shell_pid);

    // setpgid(shell_pid, shell_pid);
    // shell_pgid = getpgrp();

    // while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp()))
    //     kill(-shell_pgid, SIGTTIN);

    if (getpid() != getsid(0))
    {

        setpgid(shell_pid, shell_pid);
    }

    shell_pgid = getpgrp();
    // else
    // {
    //     shell_pgid = getpgid(getpid());
    // }
    // if (getpid() != getsid(0))
    // {

    //     setpgid(shell_pid, shell_pid);
    // }

    // shell_pgid = getpgid(shell_pid);

    /* Grab control of the terminal. */

    signal(SIGQUIT, SIG_IGN);

    signal(SIGTTIN, SIG_IGN);

    signal(SIGCHLD, SIG_IGN);

    tcsetpgrp(STDIN_FILENO, shell_pgid);
    signal(SIGTTOU, sigttou_handler);

    signal(SIGTSTP, sigstp_handler);
    signal(SIGINT, sigint_handler);

    char *line = NULL;

    // Handler for SIGTSTP

    // Set up a signal handler for SIGCHLD
    // signal(SIGCHLD, sigchld_handler);

    // printf("OG SHELL PGID: %d\n", shell_pid);

    // Initalize jobs to be all NULL
    for (int i = 0; i < 256; i++)
    {
        jobs[i] = NULL;
    }

    FILE *input = NULL;

    if (!interactive)
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
        fflush(stdout);
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

        j->id = -1; // Assign -1 for now, not in job list
        j->stdin = STDIN_FILENO;
        j->stdout = STDOUT_FILENO;

        int processIndex = 0;
        process *p = malloc(sizeof(process));
        p->argv = malloc(sizeof(char *) * 10);
        process *next;

        if (strcmp(commandArgs[i - 1], "&") == 0)
        {
            // printf("HAS AMPERSAND\n");
            j->hasAmpersand = 1;
            strcpy(commandArgs[i - 1], "\0");
            --i;
        }

        for (int k = 0; k < i; ++k)
        {
            // printf("Process Index: %d\n", processIndex);
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

                continue;
            }
            if (k == i - 1)
            {

                p->argc = processIndex + 1; // Num of arguments in command
                p->completed = 0;           // Assuming not completed (0) initially
                p->stopped = 0;             // Assuming not stopped (0) initially
                p->status = 0;              // Status value
                p->pid = -1;                // Assign -1 for now
                p->next = NULL;
            }

            p->argv[processIndex] = malloc(sizeof(char) * 10);
            strcpy(p->argv[processIndex], commandArgs[k]);

            processIndex++;

            // Assign values to its members
            if (firstProcess)
            {
                j->first_process = p;
                firstProcess = 0;
            }
        }

        foreground = j;

        // Check if it's a built in command

        runJob(j);

        memset(commandArgs, 0, sizeof(commandArgs));

        // Remove any background jobs that have terminated
        removeTerminatedJobs();

        // printf("NUMJOBS: %d\n", numJobs);
        if (interactive)
        {
            printf("wsh> ");
        }
    }
    free(line);

    exit(0);
}
