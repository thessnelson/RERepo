#include "alpm.h"
#include "alpm_list.h"

#include <stdio.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <sys/ioctl.h>

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
}string_length(str2)

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

alpm_list_t* filter(pmpkg_t* pkg_data) {
    int pkg_reason;
    alpm_list_t* output;

    if ((((((*(short *)(config + 0x4c) == 0) || 
            (pkg_reason = alpm_pkg_get_reason(pkg_data)) == 0) && 
            ((*(short *)(config + 0x4a) == 0 || 
            (pkg_reason = alpm_pkg_get_reason(pkg_data)) == 1))) && 
            ((*(short *)(config + 0x46) == 0 || (is_foreign()) != 0))) && 
            ((*(short *)(config + 0x48) == 0 || (is_unrequired()) != 0))) && 
            (*(short *)(config + 0x54) != 0)) {
        output = alpm_option_get_syncdbs();
        alpm_sync_newversion(pkg_data, output);
    }
    return output;
}

int getcols() {
    return ioctl(1, 0x5413);
}

int string_length(void* str) {
    if(str != NULL) {
        int len = strlen(str) + 1;
        wchar_t* __pwcs = (wchar_t*)calloc((long)len, 4);
        return wcswidth(__pwcs, (long)mbstowcs(__pwcs, str, (long)len));
    }

    return 0;
}

void indent_print(char* input_str, int len) {
    int str_len;
    size_t input_len;
    uint len;
    char *input_str;
    int next_len;
    uint current_len;
    wchar_t *buf;
    wchar_t *temp;
    wchar_t *next_space;

    if (input_str != (char *)0x0) {
        if ((getcols() == 0) || (getcols() < (int)len)) {
            printf("%s",input_str);
        } else {
            input_len = strlen(input_str);
            str_len = (int)input_len + 1;
            buf = (wchar_t *)calloc((long)str_len,4);
            input_len = mbstowcs(buf,input_str,(long)str_len);
    
            if ((buf != (wchar_t *)0x0) && (current_len = len, (int)input_len != 0)) {
                while (*buf != L'\0') {
                    if (*buf == L' ') {
                        buf = buf + 1;
                        
                        if ((buf != (wchar_t *)0x0) && (*buf != L' ')) {
                            next_space = wcschr(buf,L' ');
                            
                            if (next_space == (wchar_t *)0x0) {
                                input_len = wcslen(buf);
                                next_space = buf + input_len;
                            }
                            
                            next_len = 0;
                            temp = buf;
                            
                            while (temp < next_space) {
                                str_len = wcwidth(*temp);
                                next_len = next_len + str_len;
                                temp = temp + 1;
                            }
                            
                            if (next_len < (int)(extraout_EAX - current_len)) {
                                putchar(0x20);
                                current_len = current_len + 1;
                            } else {
                                printf("\n%-*s",(ulong)len,"");
                                current_len = len;
                            }
                        }
                    } else {
                        printf("%lc",(ulong)(uint)*buf);
                        str_len = wcwidth(*buf);
                        current_len = current_len + str_len;
                        buf = buf + 1;
                    }
                }
            }
        }
    }

    return;
}

void string_display(char* str1, void* str2) {
    if (str1 != NULL) {
        printf("%s", str1);
    }

    if (str2 == NULL || (*str2 == '\0')) {
        printf("None");
    }

    else {
        indent_print(str2, string_length(str2));
    }

    putchar(10);
}

void list_display(char* field, alpm_list_t* list) {
    void* data;
    int new_len;

    int field_len = 0;

    if (field != 0) {
        field_len = string_length(field) + 1;
        printf("%s ",field);
    }

    if (list == 0) {
        puts("None"));
    } else {
        data = alpm_list_getdata(list);
        printf("%s",data);

        int data_len_head = string_length(data);
        new_len = field_len + data_len_head;

        for (alpm_list_t* i = alpm_list_next(list); i != 0; i = alpm_list_next(i)) {
            data = alpm_list_getdata(i);
            int data_len_current = string_length(data);
            
            if ((field_len < getcols()) && (getcols() <= data_len_current + new_len + 2)) {
                new_len = field_len;
                
                putchar(10);

                for (int j = 1; j <= field_len; j = j + 1) {
                    putchar(0x20);
                }
            } else if (new_len != field_len) {
                printf("  ");
                new_len = new_len + 2;
            }

            printf("%s",data);
            new_len = new_len + data_len_current;
        }
        
        putchar(10);
    }
    
    return;
}

int get_backup_file_status(alpm_list_t* val, char* cs) {
    char *__s1;
    char buf [4104];

    snprintf(buf, 0x1000, "%s%s", cs, val);
    int output = access(buf, 4);
    if (output == 0) {
        __s1 = (char *)alpm_compute_md5sum(local_1018);

        if (__s1 == (char *)0x0) {
            pm_fprintf(gettext("could not calculate checksums for %s\n"));
        }
        else {
            strcmp(__s1, cs);
        }
    }
    
    else {
    __errno_location();
    }

    return output;
}

void dump_pkg_backups(pmpkg_t* pkg_data) {
    const char* root = alpm_option_get_root();
    printf("Backup Files:\n");

    alpm_list_t* pkg_backup = alpm_pkg_get_backup(pkg_data);

    if(pkg_backup == 0) {
        printf((char*)gettext("(none)\n"));
    } else {
        for (alpm_list_t* i = alpm_pkg_get_backup(pkg_data); i != 0; i = alpm_list_next(i)) {
            char* buf = (char*)alpm_list_getdata(i);
            buf = strdup(buf);

            char* current = strchr(buf, 9);

            if(current != NULL) {
                *current = '\0';
                printf("%s\t%s%s\n", get_backup_file_status(i, current + 1), root, buf);
            }
        }
    }
}

void dump_pkg_full(pmpkg_t* pkg_data, int config) {
    int pkg_reason;
    tm *ts;
    long pkg_size;
    char* dep_info;
    
    time_t pkg_build_date;
    time_t pkg_install_date;
    
    char* output_str;
    alpm_list_t* require_list;
    alpm_lis_t* new_list;
    pmdepend_t* depend;

    char pkg_build_date_buffer[0x32];
    char pkg_install_date_buffer[0x32];

    if (pkg_data != 0) {
        pkg_build_date = alpm_pkg_get_builddate(pkg_data);
        
        if (pkg_build_date != 0) {
            ts = localtime(&pkg_build_date);
            strftime((char *)&pkg_build_date_buffer, 0x32, "%c", ts);
        }
        
        pkg_install_date = alpm_pkg_get_installdate(pkg_data);
        
        if (pkg_install_date != 0) {
            ts = localtime(&pkg_install_date);
            strftime((char *)&pkg_install_date_buffer, 0x32, "%c", ts);
        }
        
        pkg_reason = alpm_pkg_get_reason(pkg_data);
        
        if (pkg_reason == 0) {
            output_str = gettext("Explicitly installed");
        } else if (pkg_reason == 1) {
            output_str = gettext("Installed as a dependency for another package");
        } else {
            output_str = gettext("Unknown");
        }

        for (alpm_list_t* i = alpm_pkg_get_depends(pkg_data); i != 0; i = alpm_list_next(i)) {
            depend = alpm_list_getdata(i);
            dep_info = alpm_dep_compute_string(depend);
            new_list = alpm_list_add(new_list,dep_info);
        }

        if ((0 < config) || (config < -1)) {
            require_list = alpm_pkg_compute_requiredby(pkg_data);
        }

        string_display(gettext("Name           :"), alpm_pkg_get_name(pkg_data));
        string_display(gettext("Version        :"), alpm_pkg_get_version(pkg_data));
        string_display(gettext("URL            :"), alpm_pkg_get_url(pkg_data));
        list_display(gettext("Licenses       :"), alpm_pkg_get_licenses(pkg_data));
        list_display(gettext("Groups         :"), alpm_pkg_get_groups(pkg_data));
        list_display(gettext("Provides       :"), alpm_pkg_get_provides(pkg_data));
        list_display(gettext("Depends On     :"), new_list);
        list_display_linebreak(gettext("Optional Deps  :"), alpm_pkg_get_optdepends(pkg_data));

        if ((0 < config) || (config < -1)) {
            list_display(gettext("Required By    :"), require_list);
        }

        list_display(gettext("Conflicts With :"), alpm_pkg_get_conflicts(pkg_data));
        list_display(gettext("Replaces       :"), alpm_pkg_get_replaces(pkg_data));

        if (config < 0) {
            pkg_size = alpm_pkg_get_size(pkg_data);
            dep_info = gettext("Download Size  : %6.2f K\n");
            printf((char *)((double)pkg_size / 1024.0),dep_info);
        }

        if (config == 0) {
            pkg_size = alpm_pkg_get_size(pkg_data);
            dep_info = gettext("Compressed Size: %6.2f K\n");
            printf((char *)((double)pkg_size / 1024.0),dep_info);
        }

        pkg_size = alpm_pkg_get_isize(pkg_data);
        dep_info = gettext("Installed Size : %6.2f K\n");
        printf((char *)((double)pkg_size / 1024.0),dep_info);

        string_display(gettext("Packager       :"), alpm_pkg_get_packager(pkg_data));
        string_display(gettext("Architecture   :"), alpm_pkg_get_arch(pkg_data));
        string_display(gettext("Build Date     :"), pkg_build_date_buffer);

        if (0 < config) {
            string_display(gettext("Install Date   :"), pkg_install_date_buffer);
            string_display(gettext("Install Reason :"), output_str);
        }

        if (-1 < config) {
            pkg_reason = alpm_pkg_has_scriptlet(pkg_data);
            char* script_str;

            if (pkg_reason == 0) {
                script_str = gettext("No");
            } else {
                script_str = gettext("Yes");
            }

            string_display(gettext("Install Script :"), script_str);
        }

        if (config < 0) {
            string_display(gettext("MD5 Sum        :"), alpm_pkg_get_md5sum(pkg_data));
        }

        string_display(gettext("Description    :"), alpm_pkg_get_desc(pkg_data));
        
        if (1 < config) {
            dump_pkg_backups(pkg_data);
        }

        putchar(10);
    }
}

void dump_pkg_files(pmpkg_t* pkg_data, int config) {
    const char* pkg_name = alpm_pkg_get_name(pkg_data);
    const char* root = alpm_option_get_root();

    for (alpm_list_t* i = alpm_pkg_get_files(pkg_data); i != 0; i = alpm_list_next(i)) {
        char* current = alpm_list_getdata(i);

        if (config == 0) {
            fprintf(stdout, "%s %s%s\n", pkg_name,r oot, current);
        }
        else {
            fprintf(stdout, "%s%s\n", root, current);
        }
    }

    fflush(stdout);
    return;
}

void dump_pkg_changelog(pmpkg_t* pkg_data) {
    char buf [4104];

    FILE* ss = alpm_pkg_changelog_open(pkg_data);
    
    if (ss == 0) {
        alpm_pkg_get_name(pkg_data);
        gettext("no changelog available for \'%s\'.\n");
        pm_fprintf();
    } else {
        while ((long out = alpm_pkg_changelog_read(buf, 0x1000, pkg_data, ss)) != 0) {
            if (out < 0x1000) {
                buf[out] = 0;
            }
            printf("%s", buf);
        }
        
        alpm_pkg_changelog_close(pkg_data, ss);
        putchar(10);
    }
}

void check(pmpkg_t* pkg_data) {
    struct stat sb;
    char buf [4104];

    char* root = (char *)alpm_option_get_root();
    size_t root_len = strlen(root);
    
    if (root_len + 1 < 0x1001) {
        strcpy(buf,root);
        const char* pkg_name = alpm_pkg_get_name(pkg_data);

        for (alpm_list_t* i = alpm_pkg_get_files(pkg_data); i != 0; i = alpm_list_next(i)) {
            root = (char *)alpm_list_getdata(i);
            size_t rlen = strlen(root);

            if (rlen + root_len + 1 < 0x1001) {
                strcpy(buf + root_len,root);
                int f = lstat(buf, &sb);

                if ((f != 0) && (*(short *)(config + 2) != 0)) {
                    printf("%s %s\n", pkg_name, buf);
                }
            }
            else {
                pm_fprintf(gettext("Warning: The following path is too long: %s%s\n"));
            }
        }
    }
    else {
        pm_fprintf(gettext("Warning: The following path is too long: %s%s\n"));
    }
}

void display(pmpkg_t* pkg_data) {
    if (*(short*)(config + 0x42) != 0) {
        if (*(short*)(config + 0x40) == 0) {
            dump_pkg_full(pkg_data, *(short*)(config + 0x42));
        } else {
            dump_pkg_full(pkg_data, 0);
        }
    }

    if (*(short*)(config + 0x44)) != 0) {
        dump_pkg_files(pkg_data, *(short*)(config + 0x2));
    }

    if (*(short*)(config + 0x52) != 0) {
        dump_pkg_changelog(pkg_data);
    }

    if (*(short*)(config + 0x56) != 0) {
        check(pkg_data);
    }

    if ((((*(short *)(config + 0x42) == 0) && (*(short *)(config + 0x44) == 0)) && (*(short *)(config + 0x52) == 0)) && (*(short *)(config + 0x56) == 0)) {
        if (*(short*)(config + 2) == 0) {
            printf("%s %s\n", alpm_pkg_get_name(pkg_data), alpm_pkg_get_version(pkg_data));
        } else {
            puts((char *)alpm_pkg_get_name(pkg_data));
        }
    }
}



void p_query(alpm_list_t* pm_targets) {
    alpm_list_t* syncdb;
    size_t db_count;

    pmpkg_t* pkg_data;
    pmdb_t* localdb;

    if (*(short*)(config + 0x50) == 0) {
        if (*(short*)(config + 0x100) == 0) {
            if (*(short*)(config + 0x46) == 0 || ((syncdb = alpm_option_get_syncdbs()) != 0 && (db_count = alpm_list_count(syncdb)) != 0)) {
                localdb = alpm_option_get_localdb();

                if (pm_targets == 0) {
                    if (*(short*)(config + 0x40) == 0 && (*(short*)(config + 0x4e) == 0)) {
                        for(alpm_list_t* i = alpm_db_getpkgcache(localdb); i != 0; i = alpm_list_next(i)) {
                            pkg_data = alpm_list_getdata(i);

                            if (filter(pkg_data) != 0) {
                                display(pkg_data);
                            }

                            struct timespec ts;
                            ts.tv_sec = 0;
                            ts.tv_nsec = 200000000;

                            nanosleep(&ts, NULL);
                        }
                    } else {
                        pm_printf(gettext("Error: No targets were specified. Use the -h flag for help.\n"));
                    }
                } else {
                    alpm_list_t* current = pm_targets;
                    alpm_list_t* temp;

                    if (*(short*)(config + 0x4e) == 0) {
                        while ((temp = current) != 0) {
                            char* pkg_name alpm_list_getdata(temp);

                            if (*(short*)(config + 0x40) == 0) {
                                pkg_data = alpm_db_get_pkg(local_db, pkg_name);
                            } else {
                                alpm_pkg_load(pkg_name, 1, &pkg_data);
                            }

                            if (pkg_data == 0) {
                                pm_printf(gettext("Error: The package \"%s\" not found.\n"));
                            } else {
                                if (filter(pkg_data) != 0) {
                                    display(pkg_data);
                                }

                                if (*(short*)(config + 0x40) != 0) {
                                    pkg_data = NULL;
                                }
                            }

                            current = alpm_list_next(temp);
                        }
                    } else {
                        query_fileowner();
                    }
                }
            }
        } else {
            query_group();
        }
    } else {
        query_search();
    }
}

void p_sync(alpm_list_t* pm_targets) {

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
        p_query(pm_targets);
    } else if (*config == 5) {
        p_sync(pm_targets);
    } else {
        pm_printf(gettext("Error: No operation was specified. Use the -h flag for help.\n"));
        exit(1);
    }

    return 0;
}
