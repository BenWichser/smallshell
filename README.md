# smallshell
 Small shell program, written in C.   The overall intent was to use external process execution, input/output redirection, and signal handling.

 Shell program displays prompt (`:`), which accepts user input.  User input is of the form `<command> [<args>] [< input_file] [> output_file] [&]`, where

 1. `<command>` is the given command
 2. `[<args>]` are the (optional) command-line arguments.
 3. `[< input_file]` is the (optional) redirect of input from a file
 4. `[< output_file]` is the (optional) redirect of output to a file
 5. `[&]` is the (optional) indicator to run the command in the background.  Background commands are indicated via user feedback: the \<pid> of the background command is sent to stdout, and and indicator is displayed when a background command completes.

 The arguments are generally provided in the order above, which indicates following these rules:

 *  The background indicator must be at the end of the command.
 *  The input and output redirect must be at the end (or just before the background indicator, if that is present), but they may be in either order.  That is `<command> [<args>] <input_file >output_file` and `<command> [<args>] >output_file <input_file` are equivalent to each other.

 There are additional considerations:

 1.  Any input line that begins with a `#` is treated as a comment, and the input is ignored.
 2.  Any blank input (blank line entry or comment entry) results in a new prompt (`:`).
 3.  There are two ways in which signals are specially handled:

        * SigInt (Ctrl-C) is handled according to the following specifications:
        
            1.  The shell ignores SigInt.
            2.  Background processes, if any, ignore SigInt.
            3.  Foregroung processes, if any, immediately terminate upon receiving SigInt.

        * SigStp (Ctrl-Z) is handled according to the following specifications:

            1.  All background and foreground processes ignore SigStp.
            2.  The first time the shell receives SigStp, it enters "foreground-only" mode: the user is notified of this mode, and all processes now run only as foreground processes (commands with `&` at the end have the `&` ignored and are run as if background status was not requested). 
            3.  Any other use of SigStp toggles the shell between "foreground-only" mode and not being in "foreground-only" mode.  Each time, the user is presented with information indicating this change.
4.  Any use of `$$` anywhere in the user input is changed to the shell's \<pid>.
5.  If the user command is `exit`, then the shell terminates all background processes and exit.
6.  If the user command is `cd`, then the shell changes its current working directory.  (It starts with the working directory being the directory in which `smallsh` resides.)
7.  If the user command is `status`, then the shell prints the exit status or terminating signal of the last foreground process.
8.  Any command besides `exit`, `cd`, and `status` is handled by executing external processes.  This means that common *nix commands (`ls`, `mv`, etc) and even the compilation of `smallsh` can be performed within `smallsh`.

