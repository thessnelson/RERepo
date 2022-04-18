#include "alpm.h"
#include "alpm_list.h"

#include <stdio.h>
#include <signal.h>
#include <stdarg.h>

#include <sys/utsname.h>

// Global variables
int* config;

// Helper functions
void pm_vfprintf(const char* template) {
    va_list ap;
    vfprintf(stderr, template, ap);
}

void pm_printf(const char* template) {
    pm_vfprintf(template);
}

void handler(int input) {
    fileno(stdout);
    fileno(stderr);

    if (input == 0xb) {
        strlen("Error: Segmentation fault!\n");
        xwrite();

        exit(0xb);
    } if (input == 2) {
        strlen("\nInterrupt signal received.\n");
        xwrite();
        
        if (alpm_trans_interrupt() == 0) {
            return;
        }
        xwrite();
    }

    exit(input);
} 

int main() {
    // Set up signals
    struct sigaction new_action, old_action; 
    new_action.sa_handler = handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    sigaction(2, NULL, &old_action);

    if (old_action.sa_handler != SIG_IGN) {
        sigaction(SIGINT, &new_action, NULL);
    }
    sigaction(SIGHUP, NULL, &old_action);

    if (old_action.sa_handler != SIG_IGN) {
        sigaction(SIGHUP, &new_action, NULL);
    }
    sigaction(SIGTERM, NULL, &old_action);

    if (old_action.sa_handler != SIG_IGN) {
        sigaction(SIGTERM, &new_action, NULL);
    }

    // alpm setup
    struct utsname buf;

    localize();
    uname(buf);

    char buffer[100];
    snprintf(buffer, 100, "pkgmis/%s (%s %s) libalpm/%s", "3.5.4", buf, buf.machine, alpm_version());
    setenv("HTTP_USER_AGENT", buffer, 0);
    config_new();

    if (alpm_initialize() == -1) {
        pm_printf(gettext("Error: Necessary libraries could not be initialized\n"));
        exit(1);
    }

    

    // Root access commands
    parseconfig();
    needs_root();

    if (*config == 4) {
        p_query();
    } else if (*config == 5) {
        p_sync();
    } else {
        pm_printf(gettext("Error: No operation was specified. Use the -h flag for help.\n"));
        exit(1);
    }

    return 0;
}
