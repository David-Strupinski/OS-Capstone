#include <ctype.h>
#include <stdio.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/deferred.h>
#include <aos/systime.h>
#include <fs/ramfs.h>

#define LINE_LENGTH 78
#define MAX_TOKENS 32

uint32_t var_exit_code = 0;
domainid_t var_exit_pid = 0;

void *fs;
ramfs_handle_t current_dir_handle;
char *current_path;

// exact string comparison (strings must be null terminated)
static int is_string(char *a, char *b) {
    return strcmp(a, b) == 0 && strlen(a) == strlen(b);
}

// handle a single command (represented as a string of tokens)
static void handle_command(char **tokens, int num_tokens) {
    struct aos_rpc *rpc = aos_rpc_get_serial_channel();
    if (is_string(tokens[0], "echo")) {
            // echo variable if present
            if (num_tokens == 2 && is_string(tokens[1], "$!")) {
                printf("%u\n", var_exit_pid);
                return;
            }
            if (num_tokens == 2 && is_string(tokens[1], "$?")) {
                printf("%u\n", var_exit_code);
                return;
            }

            // echo following token back to user
            if (num_tokens > 1) printf("%s", tokens[1]);
            printf("\n");
        } else if (is_string(tokens[0], "run_memtest")) {
            // determine size
            int64_t size = BASE_PAGE_SIZE;  // default to one page
            if (num_tokens > 1) {
                int64_t new_size = strtol(tokens[1], NULL, 10);
                if (new_size > 0) {
                    size = new_size;
                } else {
                    printf("Invalid size. Deafulting to 4096.\n");
                }
            }

            // malloc some memory
            char *memory = malloc(size);
            if (memory == NULL) {
                printf("Couldn't allocate memory.\n");
                return;
            }

            // Write to the memory.
            for (int64_t i = 0; i < size; i++) {
                memory[i] = i % sizeof(char);
            }

            // Read from the memory.
            bool failed = false;
            for (int64_t i = 0; i < size; i++) {
                if (memory[i] != i % sizeof(char)) {
                    failed = true;
                    printf("Memory test failed %lu bytes into chunk.\n");
                    break;
                }
            }
            if (!failed) {
                printf("Memory test succeeded.\n");
            }
        } else if (is_string(tokens[0], "run")) {
            // spawn a process
            if (num_tokens > 1) {
                // check that we aren't trying to run the shell or init
                if (is_string(tokens[1], "shell") || is_string(tokens[1], "init")) {
                    printf("%s is already running.\n", tokens[1]);
                    return;
                }

                // create cmdline
                char cmdline[LINE_LENGTH];
                int cmdline_index = 0;
                for (int i = 1; i < num_tokens; i++) {
                    // ignore &
                    if (i == num_tokens - 1 && is_string(tokens[i], "&")) continue;

                    strcpy(&cmdline[cmdline_index], tokens[i]);
                    cmdline_index += strlen(tokens[i]);
                    cmdline[cmdline_index] = ' ';
                    cmdline_index++;
                }

                // spawn the process
                domainid_t pid;
                errval_t err = aos_rpc_proc_spawn_with_cmdline(rpc, cmdline, disp_get_core_id(), &pid);
                if (err_is_fail(err) || pid == SPAWN_ERR_PID) {
                    printf("unable to run %s\n", tokens[1]);
                    return;
                }

                // wait on the process if requested
                if (!is_string(tokens[num_tokens - 1], "&")) {
                    int status;
                    aos_rpc_proc_wait(rpc, pid, &status);
                    printf("%s exited with code %d\n", tokens[1], status);
                    var_exit_code = status;
                } else {
                    // just give the program a bit of time to put out the initial output
                    barrelfish_usleep(100000);
                }

                // update PID variable
                var_exit_pid = pid;
            } else {
                printf("usage: run [cmdline] [&]\n");
            }
        } else if (is_string(tokens[0], "oncore")) {
            // TODO: we'll need to implement a UMP call if we want putchar and getchar 
            // to work on other cores

            // spawn a process
            if (num_tokens > 2) {
                long core = strtol(tokens[1], NULL, 10);
                if (core < 0 || core > 3) {
                    printf("Invalid core.\n");
                }

                // check that we aren't trying to run the shell or init
                if (is_string(tokens[2], "shell") || is_string(tokens[2], "init")) {
                    printf("%s is already running.\n", tokens[1]);
                    return;
                }

                // create cmdline
                char cmdline[LINE_LENGTH];
                int cmdline_index = 0;
                for (int i = 2; i < num_tokens; i++) {
                    // ignore &
                    if (i == num_tokens - 1 && is_string(tokens[i], "&")) continue;

                    strcpy(&cmdline[cmdline_index], tokens[i]);
                    cmdline_index += strlen(tokens[i]);
                    cmdline[cmdline_index] = ' ';
                    cmdline_index++;
                }

                // spawn the process
                domainid_t pid;
                errval_t err = aos_rpc_proc_spawn_with_cmdline(rpc, cmdline, core, &pid);
                if (err_is_fail(err) || pid == SPAWN_ERR_PID) {
                    printf("unable to run %s\n", tokens[2]);
                    return;
                }

                // wait on the process if requested
                if (!is_string(tokens[num_tokens - 1], "&")) {
                    int status;
                    aos_rpc_proc_wait(rpc, pid, &status);
                    printf("%s exited with code %d\n", tokens[1], status);
                    var_exit_code = status;
                } else {
                    // just give the program a bit of time to put out the initial output
                    barrelfish_usleep(100000);
                }

                // update PID variable
                var_exit_pid = pid;
            } else {
                printf("usage: oncore [coreid] [cmdline] [&]\n");
            }
        } else if (is_string(tokens[0], "ps")) {
            // print running processes
            printf("PID:\tName:\n");
            domainid_t *pids;
            size_t num_pids;
            aos_rpc_proc_get_all_pids(rpc, &pids, &num_pids);
            for (size_t i = 0; i < num_pids; i++) {
                char *proc_name;
                aos_rpc_proc_get_name(rpc, pids[i], &proc_name);
                printf("%d\t%s\n", pids[i], proc_name);
            }
        } else if (is_string(tokens[0], "lsmod")) {
            // print elf modules
            printf("ELF modules on boot image:\n");
            char (*names)[][MOD_NAME_LEN];
            int name_count;
            aos_rpc_list_elf_mod_names(rpc, &names, &name_count);
            for (int i = 0; i < name_count; i++) {
                printf("%s\n", (*names)[i]);
            }
        } else if (is_string(tokens[0], "help")) {
            // print a help message
            printf("Process management:\n");
            printf("\trun [cmdline] [&]\n");
            printf("\toncore [coreid] [cmdline] [&]\n");
            printf("\tps\n");

            printf("File management:\n");
            printf("\tpwd\n");
            printf("\tls\n");
            printf("\ttouch [file]\n");
            printf("\trm [file]\n");
            printf("\tcat [file]\n");
            printf("\tmkdir [dir]\n");
            printf("\trmdir [dir]\n");
            printf("\tcd [dir]\n");

            printf("Miscellaneous:\n");
            printf("\techo [string]\n");
            printf("\trun_memtest [size]\n");
            printf("\tlsmod\n");
            printf("\ttime [cmd]\n");
            printf("\thelp\n");
        } else if (is_string(tokens[0], "ls")) {
            ramfs_opendir(fs, current_path, &current_dir_handle);
            char *name = malloc(64);
            struct fs_fileinfo info;
            printf("Type\tSize\tName\n");
            while (true) {
                errval_t err = ramfs_dir_read_next(fs, current_dir_handle, &name, &info);
                if (err == FS_ERR_INDEX_BOUNDS) {
                    break;
                }
                printf("%s\t%lu\t%s\n", info.type ? "Dir" : "File", info.size, name);
            }
            ramfs_closedir(fs, current_path);
        } else if (is_string(tokens[0], "mkdir")) {
            if (num_tokens != 2) {
                printf("usage: mkdir [dir]\n");
                return;
            }
            char *new_path = malloc(64);
            strcpy(new_path, current_path);
            strcpy(new_path + strlen(current_path), tokens[1]);
            errval_t err = ramfs_mkdir(fs, new_path);
            if (err_is_fail(err)) {
                printf("Unable to create directory.\n");
            }
            free(new_path);
        } else if (is_string(tokens[0], "rmdir")) {
            if (num_tokens != 2) {
                printf("usage: rmdir [dir]\n");
                return;
            }
            char *new_path = malloc(64);
            strcpy(new_path, current_path);
            strcpy(new_path + strlen(current_path), tokens[1]);
            errval_t err = ramfs_rmdir(fs, new_path);
            if (err_is_fail(err)) {
                printf("Unable to remove directory.\n");
            }
            free(new_path);
        } else if (is_string(tokens[0], "cd")) {
            if (num_tokens != 2) {
                printf("usage: cd [dir]\n");
                return;
            }
            ramfs_handle_t new_dir_handle;
            errval_t err = ramfs_opendir(fs, tokens[1], &new_dir_handle);
            if (err_is_fail(err)) {
                printf("Couldn't change directories.\n");
                return;
            }
            current_dir_handle = new_dir_handle;
            strcpy(current_path, tokens[1]);
            if (current_path[strlen(current_path) - 1] != '/') {
                strcpy(current_path + strlen(current_path), "/");
            }
        } else if (is_string(tokens[0], "pwd")) {
            if (num_tokens != 1) {
                printf("usage: pwd\n");
                return;
            }
            printf("%s\n", current_path);
        } else if (is_string(tokens[0], "touch")) {
            if (num_tokens != 2) {
                printf("usage: touch [file]\n");
                return;
            }
            char *new_path = malloc(64);
            strcpy(new_path, current_path);
            strcpy(new_path + strlen(current_path), tokens[1]);
            ramfs_handle_t handle;
            errval_t err = ramfs_create(fs, new_path, &handle);
            if (err_is_fail(err)) {
                printf("Unable to create file.\n");
                return;
            }
            ramfs_close(fs, handle);
            free(new_path);
        } else if (is_string(tokens[0], "rm")) {
            if (num_tokens != 2) {
                printf("usage: rm [file]\n");
                return;
            }
            char *new_path = malloc(64);
            strcpy(new_path, current_path);
            strcpy(new_path + strlen(current_path), tokens[1]);
            errval_t err = ramfs_remove(fs, new_path);
            if (err_is_fail(err)) {
                printf("Unable to remove file.\n",err_getstring(err));
            }
            free(new_path);
        } else if (is_string(tokens[0], "cat")) {
            if (num_tokens != 2) {
                printf("usage: cat [file]\n");
                return;
            }
            char *new_path = malloc(64);
            strcpy(new_path, current_path);
            strcpy(new_path + strlen(current_path), tokens[1]);
            ramfs_handle_t handle;
            errval_t err = ramfs_open(fs, new_path, &handle);
            free(new_path);
            if (err_is_fail(err)) {
                printf("Unable to open file.\n",err_getstring(err));
                return;
            }
            char bytes[17];  // small to demonstrate we can read arbitrary lengths
            size_t bytes_read;
            do {
                err = ramfs_read(fs, handle, &bytes, 16, &bytes_read);
                if (err_is_fail(err)) {
                    printf("Error while reading file.\n");
                    return;
                }
                bytes[bytes_read] = 0;
                printf("%s", bytes);
            } while (bytes_read == 16);
            printf("\n");
        } else {
            printf("Unknown command \"%s\".\n", tokens[0]);
        }
}

// main command loop
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    struct aos_rpc *rpc = aos_rpc_get_serial_channel();

    // set up filesystem
    current_path = (char *)malloc(64);
    strcpy(current_path, "/");
    errval_t err = ramfs_mount(current_path, &fs);
    DEBUG_ERR_ON_FAIL(err, "couldn't mount RAMFS\n");
    barrelfish_usleep(500000);

    // prompt user for a command and execute it
    char line[LINE_LENGTH + 1];
    char c;
    int length;
    while (true) {
        aos_rpc_serial_putchar(rpc, '$');
        aos_rpc_serial_putchar(rpc, ' ');
        length = 0;
        while (length < LINE_LENGTH) {
            aos_rpc_serial_getchar(rpc, &c);
            aos_rpc_serial_putchar(rpc, c);
            line[length] = c;
            if (c == 127) {
                if (length > 0) {
                    // backspace was pressed
                    length--;
                    aos_rpc_serial_putchar(rpc, '\b');
                    aos_rpc_serial_putchar(rpc, ' ');
                    aos_rpc_serial_putchar(rpc, '\b');
                }
            } else if (c == '\r') {
                // start a new line and evaluate the command
                printf("\n");
                break;
            } else {
                length++;
            }
        }

        // parse string into tokens
        char *tokens[MAX_TOKENS];
        int num_tokens = 0;
        for (int i = 0; i < length; i++) {
            if (!isspace(line[i])) {
                if (line[i] == '\"') {
                    // anything in quotes is treated as one token
                    i++;
                    tokens[num_tokens] = &line[i];
                    while (i < length && line[i] != '\"') i++;
                } else {
                    // the token extends until the next whitespace
                    tokens[num_tokens] = &line[i];
                    while (!isspace(line[i])) i++;
                }
                
                // mark the end of the token with a null character
                line[i] = '\0';
                num_tokens++;
            }
        }

        // skip empty lines
        if (num_tokens == 0) continue;

        // handle command
        if (is_string(tokens[0], "time")) {
            // chop off the first token and time the command
            for (int i = 0; i < num_tokens - 1; i++) {
                tokens[i] = tokens[i + 1];
            }
            num_tokens--;
            uint32_t before_time = systime_now();
            handle_command(tokens, num_tokens);
            uint32_t after_time = systime_now();
            uint32_t microseconds = systime_to_us(after_time - before_time);
            printf("Command completed in %d microseconds.\n", microseconds);
        } else {
            handle_command(tokens, num_tokens);
        }
    }
    return EXIT_SUCCESS;
}