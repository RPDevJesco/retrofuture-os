/**
 * shell.h - RetroFuture OS Command Shell
 *
 * A standalone, reusable shell that works with any I/O backend.
 * Can be used in kernel mode, user mode, or as a remote shell.
 *
 * Usage:
 *   1. Create a shell_io_t with your I/O callbacks
 *   2. Call shell_init() with the I/O interface
 *   3. Call shell_run() for interactive mode, or
 *      shell_execute() for single commands
 */

#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define SHELL_CMD_BUF_SIZE   256
#define SHELL_MAX_ARGS       16
#define SHELL_MAX_COMMANDS   64
#define SHELL_MAX_PATH       128
#define SHELL_PROMPT_DEFAULT "> "

/* ============================================================================
 * Shell I/O Interface
 *
 * This abstraction allows the shell to work with different backends:
 *   - Direct terminal (kernel mode)
 *   - Serial port (remote shell)
 *   - User-space terminal (when we have userspace)
 * ============================================================================ */

typedef struct shell_io {
    /* Output functions */
    void (*putchar)(char c);
    void (*puts)(const char *s);
    void (*printf)(const char *fmt, ...);

    /* Input functions */
    char (*getchar)(void);               /* Blocking read */
    int  (*getchar_nonblock)(void);      /* Non-blocking, returns -1 if none */

    /* Terminal control (optional, can be NULL) */
    void (*clear)(void);                 /* Clear screen */
    void (*set_color)(uint8_t color);    /* Set text color */
    void (*get_cursor)(int *x, int *y);  /* Get cursor position */
    void (*set_cursor)(int x, int y);    /* Set cursor position */

    /* User data pointer for callbacks */
    void *user_data;
} shell_io_t;

/* ============================================================================
 * Shell Command Definition
 * ============================================================================ */

struct shell_state;  /* Forward declaration */

typedef void (*shell_cmd_fn)(struct shell_state *sh, int argc, char **argv);

typedef struct shell_command {
    const char  *name;        /* Command name */
    const char  *help;        /* Short help text */
    const char  *usage;       /* Usage string (optional) */
    shell_cmd_fn handler;     /* Command handler function */
    uint8_t      min_args;    /* Minimum required arguments */
    uint8_t      flags;       /* Command flags */
} shell_command_t;

/* Command flags */
#define SHELL_CMD_HIDDEN    0x01    /* Don't show in help */
#define SHELL_CMD_ALIAS     0x02    /* This is an alias */
#define SHELL_CMD_BUILTIN   0x04    /* Built-in command */

/* ============================================================================
 * Shell State
 * ============================================================================ */

typedef struct shell_state {
    /* I/O interface */
    shell_io_t *io;

    /* Command table */
    shell_command_t commands[SHELL_MAX_COMMANDS];
    int num_commands;

    /* Current state */
    char cwd[SHELL_MAX_PATH];         /* Current working directory */
    char prompt[32];                   /* Shell prompt */
    bool running;                      /* Shell is running */
    int  last_result;                  /* Last command result */

    /* History (optional) */
    char *history[16];                 /* Command history */
    int   history_count;
    int   history_pos;

    /* Environment/context pointer for command handlers */
    void *context;
} shell_state_t;

/* ============================================================================
 * Shell API
 * ============================================================================ */

/**
 * Initialize the shell
 *
 * @param sh      Shell state structure
 * @param io      I/O interface
 * @param context Context pointer passed to command handlers
 */
static void shell_init(shell_state_t *sh, shell_io_t *io, void *context);

/**
 * Register a command
 *
 * @param sh      Shell state
 * @param name    Command name
 * @param help    Help text
 * @param handler Command handler function
 * @param min_args Minimum arguments required
 * @return        true on success
 */
static bool shell_register(shell_state_t *sh, const char *name,
                           const char *help, shell_cmd_fn handler,
                           uint8_t min_args);

/**
 * Execute a command line
 *
 * @param sh      Shell state
 * @param cmdline Command line to execute
 * @return        Command result (0 = success)
 */
static int shell_execute(shell_state_t *sh, const char *cmdline);

/**
 * Run the interactive shell loop
 *
 * @param sh      Shell state
 */
static void shell_run(shell_state_t *sh);

/**
 * Print shell prompt
 */
static void shell_print_prompt(shell_state_t *sh);

/**
 * Find a command by name
 */
static shell_command_t *shell_find_command(shell_state_t *sh, const char *name);

/* ============================================================================
 * String Utilities (self-contained)
 * ============================================================================ */

static inline int sh_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static inline void sh_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static inline int sh_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int sh_strncmp(const char *a, const char *b, int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? ((unsigned char)*a - (unsigned char)*b) : 0;
}

static inline char *sh_strchr(const char *s, char c) {
    while (*s) {
        if (*s == c) return (char *)s;
        s++;
    }
    return NULL;
}

/* ============================================================================
 * Built-in Commands
 * ============================================================================ */

static void sh_cmd_help(shell_state_t *sh, int argc, char **argv);
static void sh_cmd_exit(shell_state_t *sh, int argc, char **argv);
static void sh_cmd_pwd(shell_state_t *sh, int argc, char **argv);
static void sh_cmd_cd(shell_state_t *sh, int argc, char **argv);
static void sh_cmd_echo(shell_state_t *sh, int argc, char **argv);

/* ============================================================================
 * Implementation
 * ============================================================================ */

static void shell_init(shell_state_t *sh, shell_io_t *io, void *context) {
    sh->io = io;
    sh->context = context;
    sh->num_commands = 0;
    sh->running = false;
    sh->last_result = 0;
    sh->history_count = 0;
    sh->history_pos = 0;

    /* Default prompt and cwd */
    sh_strcpy(sh->prompt, SHELL_PROMPT_DEFAULT);
    sh_strcpy(sh->cwd, "/");

    /* Clear history */
    for (int i = 0; i < 16; i++) {
        sh->history[i] = NULL;
    }

    /* Register built-in commands */
    shell_register(sh, "help",  "Show available commands", sh_cmd_help, 0);
    shell_register(sh, "exit",  "Exit the shell",          sh_cmd_exit, 0);
    shell_register(sh, "quit",  "Exit the shell",          sh_cmd_exit, 0);
    shell_register(sh, "pwd",   "Print working directory", sh_cmd_pwd,  0);
    shell_register(sh, "cd",    "Change directory",        sh_cmd_cd,   0);
    shell_register(sh, "echo",  "Echo text",               sh_cmd_echo, 0);
}

static bool shell_register(shell_state_t *sh, const char *name,
                           const char *help, shell_cmd_fn handler,
                           uint8_t min_args) {
    if (sh->num_commands >= SHELL_MAX_COMMANDS) {
        return false;
    }

    shell_command_t *cmd = &sh->commands[sh->num_commands++];
    cmd->name = name;
    cmd->help = help;
    cmd->usage = NULL;
    cmd->handler = handler;
    cmd->min_args = min_args;
    cmd->flags = SHELL_CMD_BUILTIN;

    return true;
}

static shell_command_t *shell_find_command(shell_state_t *sh, const char *name) {
    for (int i = 0; i < sh->num_commands; i++) {
        if (sh_strcmp(sh->commands[i].name, name) == 0) {
            return &sh->commands[i];
        }
    }
    return NULL;
}

static void shell_print_prompt(shell_state_t *sh) {
    if (sh->io->puts) {
        sh->io->puts(sh->prompt);
    }
}

/**
 * Parse command line into argc/argv
 */
static int shell_parse_args(char *cmdline, char **argv, int max_args) {
    int argc = 0;
    char *p = cmdline;

    while (*p && argc < max_args) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Handle quoted strings */
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;
            while (*p && *p != quote) p++;
            if (*p) *p++ = '\0';
        } else {
            /* Regular argument */
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }

    return argc;
}

static int shell_execute(shell_state_t *sh, const char *cmdline) {
    /* Copy command line (we'll modify it) */
    char cmd_buf[SHELL_CMD_BUF_SIZE];
    int len = sh_strlen(cmdline);
    if (len >= SHELL_CMD_BUF_SIZE) len = SHELL_CMD_BUF_SIZE - 1;

    for (int i = 0; i < len; i++) cmd_buf[i] = cmdline[i];
    cmd_buf[len] = '\0';

    /* Parse into arguments */
    char *argv[SHELL_MAX_ARGS];
    int argc = shell_parse_args(cmd_buf, argv, SHELL_MAX_ARGS);

    if (argc == 0) {
        return 0;  /* Empty command */
    }

    /* Find and execute command */
    shell_command_t *cmd = shell_find_command(sh, argv[0]);
    if (cmd) {
        if (argc - 1 < cmd->min_args) {
            if (sh->io->printf) {
                sh->io->printf("Usage: %s %s\n", cmd->name,
                              cmd->usage ? cmd->usage : "");
            }
            sh->last_result = 1;
        } else {
            cmd->handler(sh, argc, argv);
        }
    } else {
        if (sh->io->printf) {
            sh->io->printf("Unknown command: %s\n", argv[0]);
            sh->io->printf("Type 'help' for available commands.\n");
        }
        sh->last_result = 127;  /* Command not found */
    }

    return sh->last_result;
}

static void shell_run(shell_state_t *sh) {
    char cmd_buf[SHELL_CMD_BUF_SIZE];
    int cmd_len = 0;

    sh->running = true;

    if (sh->io->puts) {
        sh->io->puts("\nType 'help' for available commands.\n\n");
    }

    shell_print_prompt(sh);

    while (sh->running) {
        char c = sh->io->getchar();

        switch (c) {
            case '\n':
            case '\r':
                sh->io->putchar('\n');
                cmd_buf[cmd_len] = '\0';

                if (cmd_len > 0) {
                    shell_execute(sh, cmd_buf);
                }

                cmd_len = 0;
                if (sh->running) {
                    shell_print_prompt(sh);
                }
                break;

            case '\b':
            case 0x7F:  /* DEL */
                if (cmd_len > 0) {
                    cmd_len--;
                    sh->io->putchar('\b');
                    sh->io->putchar(' ');
                    sh->io->putchar('\b');
                }
                break;

            case 0x03:  /* Ctrl+C */
                if (sh->io->puts) {
                    sh->io->puts("^C\n");
                }
                cmd_len = 0;
                shell_print_prompt(sh);
                break;

            case 0x04:  /* Ctrl+D (EOF) */
                if (cmd_len == 0) {
                    if (sh->io->puts) {
                        sh->io->puts("\n");
                    }
                    sh->running = false;
                }
                break;

            case '\t':  /* Tab - could implement completion here */
                /* For now, just insert spaces */
                if (cmd_len < SHELL_CMD_BUF_SIZE - 4) {
                    for (int i = 0; i < 4 && cmd_len < SHELL_CMD_BUF_SIZE - 1; i++) {
                        cmd_buf[cmd_len++] = ' ';
                        sh->io->putchar(' ');
                    }
                }
                break;

            default:
                /* Printable characters */
                if (cmd_len < SHELL_CMD_BUF_SIZE - 1 && c >= 32 && c < 127) {
                    cmd_buf[cmd_len++] = c;
                    sh->io->putchar(c);
                }
                break;
        }
    }
}

/* ============================================================================
 * Built-in Command Implementations
 * ============================================================================ */

static void sh_cmd_help(shell_state_t *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (sh->io->puts) {
        sh->io->puts("\nAvailable commands:\n\n");
    }

    for (int i = 0; i < sh->num_commands; i++) {
        shell_command_t *cmd = &sh->commands[i];

        /* Skip hidden commands and aliases */
        if (cmd->flags & (SHELL_CMD_HIDDEN | SHELL_CMD_ALIAS)) {
            continue;
        }

        if (sh->io->printf) {
            sh->io->printf("  %-12s %s\n", cmd->name, cmd->help ? cmd->help : "");
        }
    }

    if (sh->io->puts) {
        sh->io->puts("\n");
    }

    sh->last_result = 0;
}

static void sh_cmd_exit(shell_state_t *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;

    sh->running = false;
    sh->last_result = 0;
}

static void sh_cmd_pwd(shell_state_t *sh, int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (sh->io->printf) {
        sh->io->printf("%s\n", sh->cwd);
    }
    sh->last_result = 0;
}

static void sh_cmd_cd(shell_state_t *sh, int argc, char **argv) {
    if (argc < 2) {
        /* cd with no args goes to root */
        sh_strcpy(sh->cwd, "/");
    } else if (sh_strcmp(argv[1], "..") == 0) {
        /* Go up one level */
        int len = sh_strlen(sh->cwd);
        if (len > 1) {
            /* Remove trailing slash if any */
            if (sh->cwd[len-1] == '/') sh->cwd[--len] = '\0';
            /* Find last slash */
            while (len > 0 && sh->cwd[len-1] != '/') len--;
            if (len == 0) len = 1;
            sh->cwd[len] = '\0';
        }
    } else if (argv[1][0] == '/') {
        /* Absolute path */
        sh_strcpy(sh->cwd, argv[1]);
    } else {
        /* Relative path - append to cwd */
        int len = sh_strlen(sh->cwd);
        if (len > 0 && sh->cwd[len-1] != '/') {
            sh->cwd[len++] = '/';
        }
        sh_strcpy(sh->cwd + len, argv[1]);
    }

    sh->last_result = 0;
}

static void sh_cmd_echo(shell_state_t *sh, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) sh->io->putchar(' ');
        sh->io->puts(argv[i]);
    }
    sh->io->putchar('\n');
    sh->last_result = 0;
}

#endif /* SHELL_H */