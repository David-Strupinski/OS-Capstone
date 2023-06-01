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
        for (length = 0; length < LINE_LENGTH; length++) {
            aos_rpc_serial_getchar(rpc, &c);
            aos_rpc_serial_putchar(rpc, c);
            line[length] = c;
            if (c == '\r') {
                printf("\n");
                break;
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
        }
    }
    return EXIT_SUCCESS;
}