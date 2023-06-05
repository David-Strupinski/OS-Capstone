#include <ctype.h>
#include <stdio.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/deferred.h>

#define LINE_LENGTH 78
#define MAX_TOKENS 32

// exact string comparison (strings must be null terminated)
static int is_string(char *a, char *b) {
    return strcmp(a, b) == 0 && strlen(a) == strlen(b);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    struct aos_rpc *rpc = aos_rpc_get_serial_channel();
    barrelfish_usleep(1000000);

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
        if (is_string(tokens[0], "echo")) {
            // echo following token back to user
            if (num_tokens > 1) printf("%s", tokens[1]);
            printf("\n");
        } else if (is_string(tokens[0], "run")) {
            // spawn a process
            if (num_tokens > 1) {
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
                if (err_is_fail(err) || pid == 99999) {
                    printf("unable to run %s\n", tokens[1]);
                    continue;
                }
                barrelfish_usleep(100000);

                // wait on the process if requested
                if (!is_string(tokens[num_tokens - 1], "&")) {
                    int status;
                    aos_rpc_proc_wait(rpc, pid, &status);  // TODO: wait() only works when the process has exited...
                    printf("%s exited with code %d\n", tokens[1], status);
                }
            } else {
                printf("usage: run [cmdline] [&]\n");
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
        } else if (is_string(tokens[0], "kill")) {
            // kill a process
            // TODO: implement actually killing a process...
            if (num_tokens > 1) {
                domainid_t pid = strtol(tokens[1], NULL, 10);
                aos_rpc_proc_kill(rpc, pid);
            } else {
                printf("usage: kill [PID]\n");
            }
        } else if (is_string(tokens[0], "lsmod")) {
            // print elf modules
            printf("Elf modules on boot image:\n");
            char (*names)[][MOD_NAME_LEN];
            int name_count;
            aos_rpc_list_elf_mod_names(rpc, &names, &name_count);
            for (int i = 0; i < name_count; i++) {
                printf("%s\n", (*names)[i]);
            }
        } else if (is_string(tokens[0], "help")) {
            printf("Available commands: echo run ps kill lsmod help\n");
        } else {
            printf("unknown command %s\n", tokens[0]);
        }
    }
    return EXIT_SUCCESS;
}