#include "alpm.h"
#include "alpm_list.h"

#include <stdio.h>
#include <signal.h>
#include <stdarg.h>

#include <sys/utsname.h>
#include <getopt.h>

// Global variables
int* config;
alpm_list_t* pm_targets;

// Helper functions
void pm_vfprintf(const char* template) {
    va_list ap;
    vfprintf(stderr, template, ap);
}

void pm_printf(const char* template) {
    pm_vfprintf(template);
}

void pm_fprintf(const char* template) {
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

void version() {
    puts("  ____            _                                        ");
    puts(" |  _ \\ __ _  ___| | ____ _  __ _  ___                     ");
    puts(" | |_) / _` |/ __| |/ / _` |/ _` |/ _ \\                    ");
    puts(" |  __/ (_| | (__|   < (_| | (_| |  __/                    ");
    puts(" |_|  _\\__,_|\\___ |_|\\_\\__,_|\\__, |\\___|                    ");
    puts(" |  \\/  (_)___ _ __ ___   __|___/__   __ _  __ _  ___ _ __ ");
    puts(" | |\\/| | / __| \'_ ` _ \\ / _` | \'_ \\ / _` |/ _` |/ _ \\ \'__|");
    puts(" | |  | | \\__ \\ | | | | | (_| | | | | (_| | (_| |  __/ |   ");
    puts(" |_|  |_|_|___/_| |_| |_|\\__,_|_| |_|\\__,_|\\__, |\\___ |_|   ");
    puts("                                           |___/           ");
    return;
}

char* mbasename(char* input) {
    return strrchr(input, '/');
}

void usage(int argc, char* argv) {
    alpm_list_t* opt_str = gettext("options");
    alpm_list_t* pkg_str = gettext("package(s)");
    alpm_list_t* usg_str = gettext("usage");
    alpm_list_t* opr_str = gettext("operation"); 

    long temp;

    if (argc == 1) {
        printf("%s:  %s <%s> [...]\n", usg_str, argv, opr_str);
        
        printf("operations:\n");
        printf("    %s {-h --help}\n", argv);
        printf("    %s {-S --sync}     [%s] [%s]\n", argv, opt_str, pkg_str);
        printf("    %s {-Q --query}    [%s] [%s]\n", argv, opt_str, pkg_str);
        printf("    %s {-V --version}\n", argv);

        printf("\nuse \'%s {-h --help}\' with an operation for available options\n", argv);
    } else if (argc == 4) {
        printf("%s:  %s {-Q --query} [%s] [%s]\n", usg_str, argv, opt_str, pkg_str);
        printf("%s:\n", opt_str);

        opt_str = gettext("  -s, --search <regex> search locally-installed packages for matching strings\n");
        opt_str = alpm_list_add(0, opt_str);
        pkg_str = gettext("  -i, --info           view package information (-ii for backup files)\n");
        temp = alpm_list_add(opt_str, pkg_str);
    } else if (argc == 5) {
        printf("%s:  %s {-S --sync} [%s] [%s]\n", usg_str, argv, opt_str, pkg_str);
        printf("%s:\n", opt_str);

        opt_str = gettext("  -i, --info           view package information\n");
        opt_str = alpm_list_add(0, opt_str);

        pkg_str = gettext("  -l, --list <repo>    view a list of packages in a repo\n");
        opt_str = alpm_list_add(opt_str, pkg_str);

        pkg_str = gettext("  -r, --refresh        refresh package list\n");
        opt_str = alpm_list_add(opt_str, pkg_str);

        pkg_str = gettext("  -s, --search <str>   search packages for package matching string\n");
        temp = alpm_list_add(opt_str, pkg_str);
    }

    for(alpm_list_t* i = temp; i != NULL; i = alpm_list_next(i)) {
        opt_str = alpm_list_getdata(i);
        printf("%s", opt_str);
    }

    return;
}

void parsearg_op(int param1, int param2) {
    short temp;
    int in_ESI;
    int in_EDI;
    
    if (param2 == 0x68) {
        if (param1 == 0) {
        config[4] = 1;
        }
    } else if (param2 < 0x69) {
        if (param2 == 0x56) {
            if (param1 == 0) {
                config[3] = 1;
            }
        } else if (param2 < 0x57) {
            if (param2 == 0x51) {
                if (param1 == 0) {
                    if (*config == 1) {
                        temp = 4;
                    } else {
                        temp = 0;
                    }
                    *config = temp;
                }
            } else if ((param2 == 0x53) && (param1 == 0)) {
                if (*config == 1) {
                    temp = 5;
                } else {
                    temp = 0;
                }
                *config = temp;
            }
        }
    }
    return;
}

void parsearg_query(int opt) {
    if (opt == 0x69) {
        config[0x42]++;
    } else if (opt == 0x73) {
        config[0x50] = 1;
    }
}

void parsearg_sync(int opt) {
    if (opt == 0x3f3) {
        config[0x68] = config[0x68] | 0x2000;
    } else if (opt < 0x3f4) {
        if (opt == 0x73) {
            config[0x60] = 1;
        } else if (opt < 0x74) {
            if (opt == 0x72) {
                config[0x5e] = config[0x5e] + 1;
            } else if (opt < 0x73) {
                if (opt == 0x69) {
                    config[0x5c] = config[0x5c] + 1;
                } else if (opt == 0x6c) {
                    config[0x44] = 1;
                }
            }
        }
    }

    return;
}

void parseargs(int argc, char** argv) {
    int opt;
    int option_index = 0;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"sync", optional_argument, 0, 'S'},
        {"query", optional_argument, 0, 'Q'},
        {"info", optional_argument, 0, 'i'},
        {"list", required_argument, 0, 'l'},
        {"refresh", no_argument, 0, 'r'},
        {"search", required_argument, 0, 's'},
        {0, NULL, 0, NULL}
    }

    while ((opt = getopt_long(argc, argv, "QSVhilrs", &long_options, &option_index)) > 0) {
        if (opt != 0) {
            parsearg_op(opt, 0);
        }

        if (*config == 0) {
            pm_printf(gettext("Error: Please only use one operation at a time.\n"));
        } else if (config[4] == 0) {
            if (config[3] == 0) {
                optind = 1;

                do {
                    opt = getopt_long(argc, argv, "QSVhilrs",&long_options, &option_index);
                    if (opt <= 0) {
                        for(; optind < argc; optind++) {
                            pm_targets = alpm_list_add(pm_targets, strdup(argv[optind]));
                        }
                    }
                } while (opt == 0);

                parsearg_op(opt, 1);

                if (*config == 4) {
                    parsearg_query(opt);
                } else if (*config == 5) {
                    parsearg_sync(opt);
                }

                pm_printf(gettext("Error: You selected an invalid option. Please try again.\n"));
            } else {
                version();
            }
        } else {
            usage(argc, mbasename(argv[0]));
        }
    }
}

void config_new() {
    char *conf_path;
    
    config = (int *)calloc(1, 0x98);

    if (config == NULL) {
        pm_fprintf(gettext("malloc failure: could not allocate %zd bytes\n"));
    } else {
        *config = 1;
        config[7] = 3;
        conf_path = strdup("/opt/pkgmis/etc/pkgmis.conf");
        *(char **)(config + 0x10) = conf_path;
    }

    return;
}

void needs_root() {
    int pass;
    char *input;
    char* message = "The password is incorrect";

    input = getpass("Password: ");
    pass = strncmp(input, message + 16, 9);

    if (pass == 0) {
        return;
    }

    fwrite("Checking password.", 1, 0x12, stderr);

    for (int i = 0; i < 3; i++) {
        sleep(1);
        putc(0x2e, stderr);
    }

    printf("\n%s", message);
    exit(1);
}

void p_query() {

}

void p_sync() {

}

int main(int argc, char** argv) {
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

    alpm_option_set_root('/');
    alpm_option_set_dbpath("/opt/pkgmis/var/lib/pkgmis/");
    alpm_option_set_logfile();

    parseargs(argc, argv);

    int stdin_no = fileno(stdin);
    stdin_no = isatty(stdin_no);

    // Not too sure what these structures are used for 
    char temp_arr[4104];
    ushort** temp_arr_ref;

    if (stdin_no == 0) {
        alpm_list_t* temp = alpm_list_find_str(pm_targets, '-');

        if (temp != NULL) {
            int i = 0;
            pm_targets = alpm_list_remove_str(pm_targets, '-', 0);

            while (i < 0x1000) {
                stdin_no = fgetc(stdin);
                temp_arr[i] = (char)stdin_no;

                if (temp_arr[i] == 0xff) {
                    break;
                }

                if((*temp_arr_ref)[temp_arr[i]] & 0x2000 == 0) {
                    i++;
                } else if (i > 0) {
                    temp_arr[i] = 0;
                    char* temp_str = strdup((char*)temp_arr);

                    pm_targets = alpm_list_add(pm_targets, temp_str);
                    i = 0;
                }
            }

            if(i > 0xfff) {
                pm_printf(gettext("Error: Buffer overflow detected while parsing arguments!\n"));
                exit(1);
            }

            if(i > 0) {
                temp_arr[i] = 0;
                char* temp_str = strdup((char*)temp_arr);

                pm_targets = alpm_list_add(pm_targets, temp_str);
            }

            FILE* ss = stdin;
            char* cter_out = ctermid((char*)NULL);
            ss = freopen(cter_out, "r", ss);

            if (ss == NULL) {
                pm_printf(gettext("Error: stdin could not be reopened.\n"));
            }
        }
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
