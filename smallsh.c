/* Author: Ben Wichser
 * Date: 3 November 2020
 * Project:  Smallsh: a minimal interactive shell that accepts commands,
 *  uses foreground and background tasks, handles some signal interrupts,
 *  and has some other limited functionality as described on Canvas page for
 *  Oregon State University's CS344 course (Fall 2020).
 */

// Includes
#include<fcntl.h>           // open
#include<math.h>            // floor, log10
#include<signal.h>          // sigset_t
#include<stdbool.h>         // bool
#include<stdio.h>           // printf, getchar
#include<stdlib.h>          // NULL, EXIT_SUCCESS, size_t, malloc
#include<string.h>          // strlen, strcpy, strcmp
#include<sys/types.h>       // pid
#include<sys/wait.h>        // waitpid
#include<unistd.h>          // chdir

// Defines
#define LINE_LENGTH     2048
#define MAX_ARGUMENTS   512

// Global Variables
bool backgroundOnly = false;

// Structs
/* argument ******************************************************************\
 * Argument represents a single argument (word) in smallsh input
 * Data Members:
 *  text (char *): actual argument text
 *  position (int): location within linked list
 *  redirInput (char *): any input redirection filepath (placed in head only)
 *  redirOutput (char *): any output redirection filepath (placed in head only)
 *  background (bool): whether command should go to background (placed in head
 *      only)
 *  next (struct argument *): used to make a linked list of arguments
 *****************************************************************************/
struct argument
{
    char *text;
    int position;
    char *redirInput;
    char *redirOutput;
    bool background;
    struct argument *next;
};

/* childProc *****************************************************************\
 * ChildProc represents a child process.
 * Data Members:
 *  id (int): <pid> of child
 *  next (struct childProc *): Location of next childProc
 * ***************************************************************************/
struct childProc
{
    int id;
    struct childProc *next;
};

/* endStatus *****************************************************************\
 * EndStatus represents the information needed to keep track of the last exit
 *  or termination status of the last foreground task.
 * Data Members:
 *  exit (bool):  Whether the last task ended as an `exit` or not.
 *  num (int): Exit or termination number.
 *****************************************************************************/
struct endStatus
{
    bool exit;
    int num;
};


// Function Prototypes
void handle_SIGTSTP(int);
void freeChildren(struct childProc *);
void freeArguments(struct argument *);
struct childProc *removeChildProc(struct childProc *, int );
struct childProc *createChildProc(struct childProc *, int );
void setOuput(struct argument *);
void setInput(struct argument *);
bool redirectIO(struct argument *);
void setBackground(struct argument *);
void createProcessArguments(char *[], struct argument *);
struct childProc *otherProcess(struct argument *, struct childProc *,
        struct endStatus *, struct sigaction, struct sigaction);
void printStatus(struct endStatus *);
void changeDir(struct argument *);
bool builtIn(struct argument *, struct childProc *, struct endStatus *);
struct argument *makeArgument(char *);
struct argument *separateInput(char *);
int check$$(char *, int, pid_t);
struct argument *getInput(pid_t);
struct childProc *checkTerminatedChildren(struct childProc *, bool);
int main(void);

// Functions
/* handle_SIGTSTP_children ***************************************************\
 * Handle_SIGTSTP_Children takes SIGTSTP and passes it to parent.
 * Accepts:
 *  sig (int): Signal number
 * Returns:
 *  Nothing
 *****************************************************************************/
void handle_SIGTSTP_children(int sig)
{
    kill(getppid(), sig);
}

/* handle_SIGTSTP ************************************************************\
 * Handle_SIGTSTP will output the correct text and switch between
 *  foreground-only and foreground-and-background modes.
 *  from linked list.
 * Accepts:
 *  sig (int):  Signal number
 * Returns:
 *  Nothing
 *****************************************************************************/
void handle_SIGTSTP(int sig)
{
    if (backgroundOnly == false) 
    {
        backgroundOnly = true;
        char *message = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write (STDOUT_FILENO, message, 52);
    }
    else
    {
        backgroundOnly = false;
        char *message = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message,32);
    }
    // fflush(stdout);
}

/* freeChildren **************************************************************\
 * FreeChildren removes the memory allocations for the childProcs.
 * Accepts:
 *  head (struct childProc *): Location of first childProc
 * Returns:
 *  Nothing
 ****************************************************************************/
void freeChildren(struct childProc *head)
{
    struct childProc *currentChild = head;
    struct childProc *oldChild = NULL;
    while (currentChild != NULL)
    {
        oldChild = currentChild;
        currentChild = currentChild->next;
        free(oldChild);
    }
    free(currentChild);
    return;
}

/* freeArguments *************************************************************\
 * FreeArguments removes the memory allocations for the arguments.
 * Accepts:
 *  head (struct argument *): Location of first argument
 * Returns:
 *  Nothing
 *****************************************************************************/
void freeArguments(struct argument *head)
{
    struct argument *currentArg = head;
    struct argument *prevArg = NULL;
    while (currentArg != NULL)
    {
        prevArg = currentArg;
        currentArg = currentArg->next;
        free(prevArg->text);
        free(prevArg->redirInput);
        free(prevArg->redirOutput);
        free(prevArg);
    }
    free(currentArg);
    return;
}

/* removeChildProc ***********************************************************\
 * RemoveChildProc removes a childProc within the linked list, if it exists.
 * Accepts:
 *  head (struct childProc *): Location of head of childProc linked list
 *  id (int): id of childProc being removed
 * Returns:
 *  head of linked list of childProcs
 *****************************************************************************/
struct childProc *removeChildProc(struct childProc *head, int id)
{
    struct childProc *current = head;
    if (current == NULL)
    {
        return head;
    }

    // if proc is the head, we make a new head
    if (current->id == id)
    {
        head = current->next;
        free(current);
        return head;
    }

    // proc is not head, so we keep track of previous and current
    current = current->next;
    struct childProc *previous = head;
    while(current != NULL)
    {
        if (current->id == id)
        {
            previous->next = current->next;
            free(current);
            return head;
        }
        previous = current;
        current = current->next;
    }

    return head;
}

/* createChildProc ***********************************************************\
 * CreateChildProc creates a childProc at the end of the linked list of
 *  childProc instances.
 * Accepts:
 *  head (struct childProc *): Location of head of childProc linked list
 *  id (int): pid of childProc being created
 * Returns:
 *  head of linked list of childProcs 
 *****************************************************************************/
struct childProc *createChildProc(struct childProc *head, int id)
{
    struct childProc *thisChild = malloc(sizeof(struct childProc));
    thisChild->next = NULL;
    thisChild->id = id;
    
    // Empty linked list of childProcs
    if (head == NULL)
    {
        head = thisChild;
        return head;
    }

    struct childProc *current = head;


    // Get to the end and add to end
    while (current-> next != NULL)
        current = current->next;
    current->next = thisChild;

    return head;
}


/* setOutput *****************************************************************\
 * SetOutput takes information from the head of the linked list of words, and 
 *  sets output accordingly (from file, or from `/dev/null` if background
 *  without file specification).
 * Accepts:
 *  head (struct argument *): Location of head of linked list of words
 * Returns:
 *  Nothing
 *****************************************************************************/
void setOutput(struct argument *head)
{
    int targetFD;
    // Specify a file name if given by user
    if (head->redirOutput || head->background)
    {
        if (head->redirOutput)
        {
            targetFD = open(head->redirOutput, 
                    O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
        else if (head->background)
        {
            targetFD = open("/dev/null", 
                    O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
        if (targetFD == -1)
        {
            perror("Cannot open file to write");
            exit(1);
        }
        else
        {
            int result = dup2(targetFD, 1);
            if (result == -1)
            {
                perror("Cannot redirect input to file");
                exit(1);
            }
        }
    }
    return;
}
 /* setInput ******************************************************************\
 * SetInput takes information from the head of the linked list of words, and 
 *  sets input accordingly (from file, or from `/dev/null` if background
 *  without file specification.
 * Accepts:
 *  head (struct argument *): Location of head of linked list of words
 * Returns:
 *  Nothing
 *****************************************************************************/
void setInput(struct argument *head)
{
    int sourceFD;

    // Specify a file name if given by user
    if (head->redirInput || head->background)
    {
        if (head->redirInput)
            sourceFD = open(head->redirInput, O_RDONLY);
        else if (head->background)
            sourceFD = open("/dev/null", O_RDONLY);
        if (sourceFD == -1)
        {
            perror("Cannot open file to read");
            exit(1);
        }
        else
        {
            int result = dup2(sourceFD, 0);
            if (result == -1)
            {
                perror("Cannot redirect input to file");
                exit(1);
            }
        }
    }
    return;
}
 
/* redirectIO ****************************************************************\
 * RedirectIO looks at linked list of input words to see if penultimate word
 *  signals IO redirection, and sets head's property accordingly.
 * Accepts:
 *  head (struct argument *): Location of head of linked list
 * Returns:
 *  True if such a redirection occurred.  False otherwise.
 *****************************************************************************/
bool redirectIO(struct argument *head)
{
    if (head->next == NULL)
        return false;
    
    struct argument *current = head->next;
    struct argument *previous = head;
    struct argument *prevprev = NULL;

    while (current->next != NULL)
    {
        prevprev = previous;
        previous = current;
        current = current->next;
    }
    if ( ! strcmp(previous->text, ">"))
    {
        head->redirOutput = calloc(strlen(current->text)+1, sizeof(char));
        strcpy(head->redirOutput, current->text);
        if (prevprev != NULL)
        {
            prevprev->next = NULL;
            freeArguments(previous);
        }

        return true;
    }
    if ( ! strcmp(previous->text, "<"))
    {
        head->redirInput = calloc(strlen(current->text)+1, sizeof(char));
        strcpy(head->redirInput, current->text);
        if (prevprev != NULL)
            prevprev->next = NULL;
        freeArguments(previous);
        return true;
    }

    return false;
}

/* setBackground *************************************************************\
 * SetBackground removes the tail from the linked list, and sets the
 *  linked list head's `background` property to true.
 * Accepts:
 *  head (struct argument *): Location of linked list of words
 * Returns:
 *  Nothing
 *****************************************************************************/
void setBackground(struct argument *head)
{
    if (backgroundOnly == false)
        head->background = true;

    if (head->next != NULL)
    {
        struct argument *current = head->next;
        struct argument *prev = head;
        while (current->next != NULL)
        {
            prev = current;
            current = current->next;
        }

        freeArguments(current);
        prev->next = NULL;
    }

    return;
}

/* createProcessArguments ****************************************************\
 * CreateProcessArguments takes the linked list of input words and turns it
 *  into an array of strings.
 * Accepts:
 *  arguments (char *[]): Empty array to fill with words
 *  head (struct argument *): Location of linked list of words
 * Returns:
 *  Nothing
 *****************************************************************************/
void createProcessArguments(char **arguments, struct argument *head)
{
    int i = 0;                              // index
    struct argument *current = head;

    while (current != NULL)
    {
        arguments[i++] = current->text;
        current = current->next;
    }

    return;
}

/* otherProcess **************************************************************\
 * OtherProcess uses `fork` and `execv` to run processes that are neither
 *  comments nor built-in processes. (Overall logic structure copied from
 *  OSU CS344 Fall 2020 Canvas page "Exploration API - Executing a New
 *  Program")
 * Accepts:
 *  head (struct argument *): Location of first argument
 *  children (struct childProc *): Location of children processes linked list
 *  exitStatus (struct endStatus *): Location of endStatus struct.  For
 *      storing information needed by`status` built-in command
 *  SIGINT_action (struct sigaction): Struct for handling SIGINT
 *  SIGTSTP_action (struct sigaction): Struct for handling SIGTSTP
 * Returns:
 *  head of linked list of childProc
 *****************************************************************************/
struct childProc *otherProcess(struct argument *head, 
        struct childProc *children, struct endStatus *exitStatus,
        struct sigaction SIGINT_action, struct sigaction SIGTSTP_action)
{

    int childStatus;
    char *arguments[MAX_ARGUMENTS] = {NULL};

    // Look for IO redirection, up to twice
    if ( redirectIO(head))
        redirectIO(head);

    pid_t newID = -8;
    newID = fork();
    switch (newID)
    {
        case -1:
            // Error forking
            perror("fork()\n");
            exit(1);
            break;
        case 0:
            // Child process
            // If foreground, change SIGINT and SIGTSTP handlers
            if (! head->background)
            {
                SIGTSTP_action.sa_handler = SIG_IGN;
                sigaction(SIGTSTP, &SIGTSTP_action, NULL);
                SIGINT_action.sa_handler = SIG_DFL;
                sigfillset(&SIGINT_action.sa_mask);
                SIGINT_action.sa_flags = 0;
                sigaction(SIGINT, &SIGINT_action, NULL);
            }
            setInput(head);
            setOutput(head);
            createProcessArguments(arguments, head);
             execvp(arguments[0], arguments);
            perror(arguments[0]);
            freeArguments(head);
            freeChildren(children);
            free(exitStatus);
            exit(1);
        default:
            // Parent process
            children = createChildProc(children, newID);
            if (! head->background)
            {
                newID = waitpid(newID, &childStatus, 0);
                if (WIFEXITED(childStatus))
                {
                    exitStatus->exit = true;
                    exitStatus->num = WEXITSTATUS(childStatus);
                }
                else
                {
                    exitStatus->exit = false;
                    exitStatus->num = WTERMSIG(childStatus);
                    if (WTERMSIG(childStatus) == 2)
                    {
                        printf("terminated by signal %d\n", 
                                WTERMSIG(childStatus));
                        fflush(stdout);
                    }
                }
                children = removeChildProc(children, newID);
            }
            else
            {
                printf("background pid is %d\n", newID);
                fflush(stdout);
            }
            break;
    }
    return children;
}

/* printStatus ***************************************************************\
 * PrintStatus prints the required information for `status` built-in command.
 * Accepts:
 *  exitStatus (struct endStatus *): Location of struct endStatus, with
 *      exit/termination information
 * Returns:
 *  Nothing
 *****************************************************************************/
void printStatus(struct endStatus *exitStatus)
{
    if (exitStatus->exit == true)
        printf("exit value ");
    else
        printf("terminated by signal ");
    printf("%d\n", exitStatus->num);
    fflush(stdout);

    return;
}
/* changeDir *****************************************************************\
 * ChangeDir changes the working directory.  It defaults to the user's HOME
 *  directory.
 * Accepts:
 *  head (struct arguemnt *): Memory location of first word in user input
 * Returns:
 *  Nothing
 *****************************************************************************/
void changeDir(struct argument *head)
{
    if (head->next == NULL)
        chdir(getenv("HOME"));
    else
        chdir(head->next->text);
    return;

}

/* killChildren **************************************************************\
 * KillChildren will kill the processes in the linked list of childProcs.
 * Accepts:
 *  head (struct childProc *): Location of first childProc
 * Returns:
 *  Nothing
 *****************************************************************************/
void killChildren(struct childProc *head)
{
    struct childProc *old = NULL;
    int childStatus;
    while (head != NULL)
    {
        old = head;
        kill(head->id, SIGTERM);
        waitpid(head->id, &childStatus, 0);
        head = old->next;
        free(old);
    }    
    return;
}

/* builtIn *******************************************************************\
 * builtIn looks at input words and checks to see the command was a
 *  comment or one of the three built-in commands: `cd, `exit`, and `status`.
 * Accepts:
 *  head (struct argument *): Memory location of first word in user input
 *  children (struct childProc *): Location of first childProc
 *  exitStatus (struct endStatus *): Location of struct endStatus, for use
 *      by `status` command.
 * Returns:
 *  True if the command was a built-in.  False, otherwise.
 *****************************************************************************/
bool builtIn(struct argument *head, struct childProc *children, 
        struct endStatus *exitStatus)
{
    if (! strcmp("exit", head->text))
    {
        killChildren(children);
        freeArguments(head);
        free(exitStatus);
        exit(EXIT_SUCCESS);
        return true;
    }
    if (! strcmp("cd", head->text))
    {
        changeDir(head);
        return true;
    }
    if (! strcmp("status", head->text))
    {
        printStatus(exitStatus);
        return true;
    }

    return false;
}

/* makeArgument **************************************************************\
 * MakeArgument takes a word (space-separated part of user input), and creates
 *  an `argument` struct.  This allows for a linked list of words for program
 *  use.
 * Accepts:
 *  token (char *): Memory location of word
 * Returns:
 *  struct argument *: Memoory location of argument struct
 *****************************************************************************/
struct argument *makeArgument(char *token)
{
    struct argument *thisWord = malloc(sizeof(struct argument));

    thisWord->text = calloc(strlen(token) + 1, sizeof(char));
    strcpy(thisWord->text, token);
    thisWord->redirInput = NULL;
    thisWord->redirOutput = NULL;
    thisWord->background = false;
    thisWord->position = -1;
    thisWord->next = NULL;
    
    return thisWord;
}

/* separateInput *************************************************************\
 * SeparateInput takes the line of user input and separates it into a linked
 *  list of char * tokens, separated by space.
 * Accepts:
 *  input (char *): Pointer to user input
 * Returns:
 *  Head of linked list of struct arguments
 *****************************************************************************/
struct argument *separateInput(char *input)
{
    struct argument *head = NULL;
    struct argument *tail = NULL;
    int counter = 0;
    char *saveptr;
    char *token = strtok_r(input, " ", &saveptr);
    
    while (token != NULL)
    {
        struct argument *thisWord = makeArgument(token);
        if (head == NULL)
        {
            head = thisWord;
            tail = thisWord;
        }
        else
        {
            tail->next = thisWord;
            tail = thisWord;
        }
        thisWord->position = counter++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    // Check for background task
    if ( ! strcmp(tail->text, "&"))
            setBackground(head);

    return head;
}
 
/* check$$ *******************************************************************\
 * Check$$ looks for an instance of `$$` in the input, for variable expansion.
 *  This should only be triggered after encountering the first instance of `$`.
 * Accepts:
 *  input (char *): Memory location of user input
 *  i: Current index within the input
 *  processID: Current PID
 * Returns:
 *  New index within the input (int)
 *****************************************************************************/
int check$$(char *input, int i, pid_t processID)
{
    char c;

    // if next character is also `$`, we replace with pid
    if ((c = getchar()) == '$')
    {
        input = input + sizeof(char) *i;
        sprintf(input, "%d", processID);
        i += floor(log10(processID)+1);
    }

    // if next character was not also `$`, we don't replace, but we might
    //  be done with line
    else
    {
        input[i++] = '$';
        // if next character was newline, we terminate string, otherwise append 
        //  next character
        if (c == '\n')
            input[i] = '\0';
        else
            input[i++] = c;
    }
    return i;
}

/* getInput ******************************************************************\
 * GetInput solicits input from the user, and places it within a linked list of
 *  `argument` structs.
 * Accepts:
 *  processID (pid_t): ProcessID of current process
 * Returns:
 *  Nothing
 *****************************************************************************/
struct argument  *getInput(pid_t processID)
{
    char input[LINE_LENGTH];            // user input
    char c;                             // each character in input
    int i = 0;                          // location within input
    struct argument *head = NULL;       // head of linked list for input

    // Get user input
    printf(": ");
    fflush(stdout);
    while ((c = getchar()) != '\n')
    {
        // if we got a `$`, check for $$ replacement
        if (c == '$')
        {
            i = check$$(input, i, processID);
            // check to make sure we didn't terminate string in `check$$`
            if (input[i] == '\0')
                break;
        }
        // if we didn't get a `$`, append character to input
        else
            input[i++] = c;
    }
    input[i] = '\0';                    // make last character a terminator
    // if input was empty or comment, we return empty linked list 
    if (i == 0 || input[0] == '#')
        return head;
    // We now know input is neither empty nor commment, so we separate input
    //  into words
    head = separateInput(input);

    return head;
}

/* checkTerminatedChildren ***************************************************\
 * CheckTerminatedChildren goes through the linked list of children processes,
 *  printing an appropriate termination message and cleaning up the linked list
 *  when it finds one.
 * Accepts:
 *  head (struct childProc *): Location of first childProc
 *  sig (bool): Whether or not this function was called by signal termination
 * Returns:
 *  head of linked list of childProcs 
 *****************************************************************************/
struct childProc *checkTerminatedChildren(struct childProc *head, bool sig)
{
    // if there are no child processes, return
    if (head == NULL)
        return head;

    // if there are child processes, we iterate and look to see if anyone has
    //  exited
    struct childProc *current = head;
    int childStatus;
    int waitStatus;
    while (current != NULL)
    {
        waitStatus = waitpid(current->id,&childStatus, WNOHANG);
        if (waitStatus != 0)
        {
            // child process has terminated
            if (WIFEXITED(childStatus))
                printf("background pid %d is done: exit value %d\n", 
                        waitStatus, WEXITSTATUS(childStatus));
            else if (sig)
                printf("terminated by signal %d\n", WTERMSIG(childStatus));
            else
                printf("background pid %d is done: terminated by signal %d\n",
                        waitStatus, WTERMSIG(childStatus));
            fflush(stdout);
            current = current->next;
            head = removeChildProc(head, waitStatus);
        }
        else
            current = current->next;
    }

    return head;
}

/* checkForegroundOnly *******************************************************\
 * CheckForegroundOnly checks to see if Foreground Only status has changed
 *  since last run.
 * Accepts:
 *  Nothing
 * Returns:
 *  True if now in foreground only mode, false if not.
 *****************************************************************************/
bool checkForegroundOnly(bool lastTime)
{
    if (lastTime != backgroundOnly)
    {
        if (lastTime == false)
        {
            printf("Entering foreground-only mode (& is now ignored)\n");
            return true;
        }
        else
        {
            printf("Exiting foreground-only mode\n");
            return false;
        }
    }
    return lastTime;
}

/* main **********************************************************************\
 * Main runs the limited shell program.
 * Accepts:
 *  Nothing
 * Returns:
 *  Integer: 0 for successful execution and 1 for unsuccessful execution
 *****************************************************************************/
int main(void)

{
    pid_t processID = getpid();             // smallsh pid
    struct childProc *children = NULL;      // children processes linked list
    struct endStatus *exitStatus = malloc(sizeof(struct endStatus));
    exitStatus->exit = true;
    exitStatus->num = 0;

    //Set up SIGINT handling, following example at from Canvas page.
    struct sigaction SIGINT_action;
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Set up SIGTSTP
    struct sigaction SIGTSTP_action;
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while(true)
    {
        // create input struct
        struct argument *head = NULL;

        // Check for any terminated children processes
        children = checkTerminatedChildren(children, false);
        // Get input
        head = getInput(processID);
        // Check against blank lines and comments 
        if (head == NULL)
        {
            continue;
        }
        // Check against built-in functions: cd, exit, status
        else if (builtIn(head, children, exitStatus))
        {
            continue;
        }
        // Run fork and execute other processes
        else
        {
            children = otherProcess(head, children, exitStatus, SIGINT_action,
                    SIGTSTP_action);
        }

        freeArguments(head);
    }

    // No idea how you would get here.  But let's free memory and leave anyway.
     freeChildren(children);
     free(exitStatus);

    return EXIT_SUCCESS;
}
