#include <stdio.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/deferred.h>

#define LINE_LENGTH 78

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    struct aos_rpc *rpc = aos_rpc_get_serial_channel();
    barrelfish_usleep(1000000);

    // prompt user for a command and execute it
    char line[LINE_LENGTH];
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

        // TODO: handle command
    }
    return EXIT_SUCCESS;
}