//
//
// safe_sqlplus - prevents having to specify password on command line
//                when invoking sqlplus
//
// Copyright (C) 2014 Ryan A. Chapman. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   1. Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//   2. Redistributions in binary form must reproduce the above copyright notice, 
//      this list of conditions and the following disclaimer in the documentation
//      and/or other materials provided with the distribution.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
// OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//
// Ryan A. Chapman, ryan@rchapman.org
// Sat May  3 22:46:30 MDT 2014
//
#include <stdio.h>    
#include <execinfo.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "safe_sqlplus.h"

// need a sighandler in case child returns while we are blocked
// on read(2).  See "this read(2) will block" later in this source file
void sighandle_sigchld(int signo) {
    int status;
    pid_t pid;

    while(1) {
        status=0;
        pid=wait3(&status, WNOHANG, (struct rusage *)NULL);
        if(pid == 0) {
            return;
        } else if(pid == -1) {
            return;
        } else {
            // if child dies with non-zero result, parent should die immediately.
            if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                exit(WEXITSTATUS(status));
            }
        }
    }
}

void sighandle_sigsegv(int signo) {
    fprintf(stderr, "Segmentation fault in child:\n");
    print_stacktrace();
}

void sighandle_sigfpe(int signo) {
    fprintf(stderr, "Floating point error in child:\n");
    print_stacktrace();
}

void sighandle_sigill(int signo) {
    fprintf(stderr, "Illegal instruction in child:\n");
    print_stacktrace();
}
// from http://www.gnu.org/software/libc/manual/html_node/Backtraces.html
void print_stacktrace(void) 
{
    void *arr[10];
    size_t sz;
    char **strings;
    size_t i;

    sz=backtrace(arr, 10);
    strings=backtrace_symbols(arr, sz);
    for(i=1; i < sz; i++)  // skip frame 0 (0 is this function, print_stacktrace())
        fprintf(stderr, "  Frame %d: %s\n", (int)i, strings[i]);

    fflush(stderr);
    free(strings);
}

// field is indexed from one
// Return: pointer to string that has contents of field, or NULL if not found
// calling method is responsible for freeing
char *cut(char *str, char delimiter, int field) {
    char *p;
    char *result;
    int num_chars=0;
    if(field == 0)
        return NULL;    // NOT FOUND: impossible to find field 0
    field--;  // make indexed from one
    p=str;
    for(int cur_field=0; ; p++) {
        if(*p == '\0' && cur_field != field)
            return NULL;  // NOT FOUND: field requested is beyond the bounds of array
        if(*p == delimiter || *p == '\0') {
            if(cur_field == field) {
                // back up to beginning of field
                for(int j=0; j < num_chars; j++)
                    p--;
                break;
            } else {
                cur_field++;
                num_chars=0;
                continue;
            }
        }
        num_chars++;
    }
    if((result=malloc(num_chars * sizeof(char))) == NULL) {
        print_stacktrace();
        PERROR("malloc()");
        exit(1);
    }
    strncpy(result, p, num_chars);
    return result;
}

// Returns pointer to string that contains basename or NULL if not possible
// calling method is responsible for freeing
char *getbasename(char *s) {
    // walk forward to first space char or NULL, then walk back until find /
    // ex: "/bin/echo this is a test", then basename would be equal to "echo"
    int num_chars=0;
    bool foundslash=false;
    char *p;
    char *basename;
    if(*s == '\0') {
        return NULL;  // not found or not possible to determine basename
    }
    p=s;
    while(*p != ' ' && *p != '\0') {
        if(*p == '/')
            foundslash=true;
        p++;
        num_chars++;
    }
    if(!foundslash)
        return strdup(s);
    while(*p != '/') {
        p--;
        num_chars--;
    }
    p++;
    if((basename=malloc(num_chars * sizeof(char))) == NULL) {
        print_stacktrace();
        PERROR("malloc()");
        exit(1);
    }
    strncpy(basename, p, num_chars);
    return basename;
}

// returns NULL if it is not possible to make argument array
char *const *make_args(char *argstr) {
    int num_args;
//    char *basename;
    char **args;
    char *p;
    if(debug)
        fprintf(stderr, "ENTER make_args(argstr=\"%s\")\n", argstr);
    if(*argstr == '\0') {
        return NULL;    // impossible to make args
    }
    p=argstr;
    num_args=1;  // we have at least the binary name in argstr, so length is at least 1
//    num_args++;  // the second arg of args is the basename of arg0, so account for that space needed
    while(*p != '\0') {
        if(*p == ' ')
            num_args++;
        p++;
    }
    // the (num_args+1) is so that we have enough space for the NULL at the end of args
    if((args=malloc((num_args+1) * sizeof(char *))) == NULL) {
        print_stacktrace();
        PERROR("malloc()");
        exit(1);
    }
    // build up two dimensional array of args
    // start at beginning of argstr, split on space, copy to args array
    // element 1 (zero indexed) must be the basename.
    // ex: "/bin/echo x y z" becomes { "/bin/echo", "echo", "x", "y", "z" }

    // arg0
    char *arg0;
    if((arg0=cut(argstr, ' ', 1)) == NULL) {
        fprintf(stderr, "Could not cut field 1 from string \"%s\"\n", argstr);
        exit(1);
    }
    *(args+0)=arg0;

    // arg1, which is just basename of arg0
    // get basename of first arg (what will become argv[0]) 
/*    if((basename=getbasename(arg0)) == NULL) {
        fprintf(stderr, "Could not get basename from string \"%s\"\n", arg0);
        exit(1);
    }
    *(args+1)=basename;
*/

    // rest of args are fields 2..n, copied to args[2..n]
//    for(int i=2; i < num_args; i++) {
      for(int i=1; i < num_args; i++) {
        char *arg=NULL;
//        if((arg=cut(argstr, ' ', i)) == NULL) {
        if((arg=cut(argstr, ' ', i+1)) == NULL) {
            fprintf(stderr, "Could not cut field %d from string \"%s\"\n", i, argstr);
            exit(1);
        }
        *(args+i)=arg;
    }
    *(args+num_args)=NULL;
    if(debug) {
        for(int i=0; *(args+i) != NULL; i++)
            fprintf(stderr, "In make_args(): returning args[%d]=%s\n", i, *(args+i));
        fflush(stderr);
    }
    return args;
}

int realloc_if_needed(char *s, int capacity) {
    int current_len, new_capacity;
    new_capacity=capacity;
    current_len=strlen(s);
    if((current_len-1) >= capacity) {
        new_capacity=capacity*2;
        if((s=realloc(s, new_capacity)) == NULL) {
            print_stacktrace();
            fprintf(stderr, "Could not realloc(s=0x%p, %d). Old capacity was %d.\n", (void *)s, new_capacity, capacity);
            PERROR("realloc()");
        }
    }
    return new_capacity;
}

char *make_connect_str(char *template, char *username, char *password) {
    char *pt, *pcs, *pu, *pp; // ptrs to template, connect string, username, password
    char *cs;   //connect string
    int capacity, i, j;

    // guess how much room we'll need
    capacity=(strlen(template)+strlen(username)+strlen(password))*sizeof(char);
    if((cs=malloc(capacity)) == NULL) {
        print_stacktrace();
        fprintf(stderr, "Could not allocate memory for connect string\n");
        PERROR("malloc()");
    }
    memset(cs, 0, capacity);
    pt=template;
    pcs=cs;
    for(i=0; *pt != '\0'; ) {
        //printf("                *pt=%c\n", *pt);
        capacity=realloc_if_needed(cs, capacity);
        if(*pt                          == '{' && 
           *(pt+1)  != '\0' && *(pt+1)  == '{' && 
           *(pt+2)  != '\0' && *(pt+2)  == 'u' && 
           *(pt+3)  != '\0' && *(pt+3)  == 's' &&
           *(pt+4)  != '\0' && *(pt+4)  == 'e' &&
           *(pt+5)  != '\0' && *(pt+5)  == 'r' &&
           *(pt+6)  != '\0' && *(pt+6)  == 'n' &&
           *(pt+7)  != '\0' && *(pt+7)  == 'a' &&
           *(pt+8)  != '\0' && *(pt+8)  == 'm' &&
           *(pt+9)  != '\0' && *(pt+9)  == 'e' &&
           *(pt+10) != '\0' && *(pt+10) == '}' &&
           *(pt+11) != '\0' && *(pt+11) == '}') {
            // we've reached {{username}} variable in template. substitute
            //printf("Found {{username}} variable at index %d\n", i);
            pu=username;
            for(j=0; *pu != '\0'; j++) {
                capacity=realloc_if_needed(cs, capacity);
                *pcs++ = *pu++;  // copy username to connect string char by char
            }
            i+=j;    // record each char written to connect string
            pt+=12;  // skip past "{{username}}" in template
        } else if(*pt                          == '{' &&
                  *(pt+1)  != '\0' && *(pt+1)  == '{' &&
                  *(pt+2)  != '\0' && *(pt+2)  == 'p' && 
                  *(pt+3)  != '\0' && *(pt+3)  == 'a' && 
                  *(pt+4)  != '\0' && *(pt+4)  == 's' && 
                  *(pt+5)  != '\0' && *(pt+5)  == 's' && 
                  *(pt+6)  != '\0' && *(pt+6)  == 'w' && 
                  *(pt+7)  != '\0' && *(pt+7)  == 'o' && 
                  *(pt+8)  != '\0' && *(pt+8)  == 'r' && 
                  *(pt+9)  != '\0' && *(pt+9)  == 'd' && 
                  *(pt+10) != '\0' && *(pt+10) == '}' && 
                  *(pt+11) != '\0' && *(pt+11) == '}') {
            // we've reached {{password}} variable in template. substitute
            //printf("Found {{password}} variable at index %d\n", i);
            pp=password;
            for(j=0; *pp != '\0'; j++) {
                capacity=realloc_if_needed(cs, capacity);
                *pcs++ = *pp++;  // copy password to connect string char by char
            }
            i+=j;    // record each char written to connect string
            pt+=12;  // skip past "{{password}}" in template
        } else {
            // not a variable. just copy the char from the template to the connect string
            //printf("t[%d]='%c'\n", i, *pt);
            *pcs++ = *pt++;
            i++;
        }
    }
    *pcs='\0';
    return cs;
}

int main(int argc, char *argv[]) {
    pid_t username_pid, pw_pid, sqlplus_pid;
    int count, status;
    int fds[2]={-1, -1};
    char buf[BUF_MAX];
    char logbuf[LOGBUF_MAX];
    char ora_username[USERNAME_MAX];
    char ora_pw[PW_MAX];
    char *const *username_args;
    char *const *pw_args;
    char sqlplus_program[SQLPLUS_MAX];
    char *const *sqlplus_args;
    char *connect_str;

    if(signal(SIGCHLD, sighandle_sigchld) == SIG_ERR ||
       signal(SIGSEGV, sighandle_sigsegv) == SIG_ERR ||
       signal(SIGFPE , sighandle_sigfpe ) == SIG_ERR ||
       signal(SIGILL , sighandle_sigill ) == SIG_ERR) {
        print_stacktrace();
        fprintf(stderr, "Could not set up signal handlers\n");
        PERROR("signal()");
        exit(1);
    }

    parse_args(argc, argv);

    // get the Oracle sqlplus username
    memset(ora_username, 0, USERNAME_MAX);
    if((username_args=make_args(username_program)) == NULL) {
        print_stacktrace();
        fprintf(stderr, "Could not make username program argument array\n");
        fflush(stderr);
        exit(1);
    }
    if(pipe(fds) == -1) {
        print_stacktrace();
        PERROR("pipe()");
        return 1;
    }
    if((username_pid=fork()) < 0) {
        print_stacktrace();
        PERROR("fork()");
        return 1;
    }
    if(username_pid > 0) {
        // parent
        close(fds[1]);  // write side of pipe
        if(read(fds[0], ora_username, USERNAME_MAX) <= 0) {
            print_stacktrace();
            fprintf(stderr, "Could not get Oracle username");
            return 1;
        }
        if(ora_username[strlen(ora_username)-1] == '\n') {
            ora_username[strlen(ora_username)-1]='\0';
        }
        if(debug)
            fprintf(stderr, "Got oracle username=\"%s\"\n", ora_username);
        // process should be gone, but if not, wait for temination and report errors if there were any
        status=0;
        waitpid(username_pid, &status, 0);
        if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Failed to execute username program (it returned %d)\n", WEXITSTATUS(status));
            fflush(stderr);
            exit(WEXITSTATUS(status));
        }
        // continue on to get Oracle sqlplus password
    } else {
        // child
        close(fds[0]);  // read side of pipe
        dup2(fds[1], fileno(stdout));  // wire up stdout (fd1) to write side of the pipe (fds[1])
        if(debug) {
            fprintf(stderr, "Exec username program: %s\n", username_args[0]);
            char *const *p;
            p=username_args;
            while(*p != NULL) {
                fprintf(stderr, " %s", *p);
                p++;
            }
            fprintf(stderr, "\n");
        }
        if(execv(username_args[0], username_args) == -1) {
            snprintf(logbuf, sizeof(logbuf), "Unable to execute username program \"%s\"", username_args[0]);
            PERROR(logbuf);
            exit(1);
        }
        return 0;
    }

    // get the oracle sqlplus password
    memset(ora_pw, 0, PW_MAX);
    if((pw_args=make_args(pw_program)) == NULL) {
        print_stacktrace();
        fprintf(stderr, "Could not make password program argument array\n");
        exit(1);
    }
    if(pipe(fds) == -1) {
        print_stacktrace();
        PERROR("pipe()");
        return 1;
    }
    if((pw_pid=fork()) < 0) {
        print_stacktrace();
        PERROR("fork()");
        return 1;
    }
    if(pw_pid > 0) {
        // parent
        close(fds[1]);  // write side of pipe
        if(read(fds[0], ora_pw, PW_MAX) <= 0) {
            print_stacktrace();
            fprintf(stderr, "Could not get Oracle password");
            return 1;
        }
        if(ora_pw[strlen(ora_pw)-1] == '\n') {
            ora_pw[strlen(ora_pw)-1]='\0';
        }
        if(debug)
            fprintf(stderr, "Got oracle password=\"%s\"\n", ora_pw);
        // process should be gone, but if not, wait for temination and report errors if there were any
        status=0;
        waitpid(pw_pid, &status, 0);
        if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Failed to execute password program (it returned %d)\n", WEXITSTATUS(status));
            fflush(stderr);
            exit(WEXITSTATUS(status));
        }
        // continue on to run sqlplus
    } else {
        // child
        close(fds[0]);  // read side of pipe
        dup2(fds[1], fileno(stdout));  // wire up stdout (fd1) to write side of the pipe (fds[1])
        if(debug)
            fprintf(stderr, "Exec password program: \"%s\"\n", pw_args[0]);
        if(execv(pw_args[0], pw_args) == -1) {
            snprintf(logbuf, sizeof(logbuf), "Unable to execute \"%s\"", pw_args[0]);
            PERROR(logbuf);
            exit(1);
        }
        return 0;
    }

    // fork/exec sqlplus, but inject the connect command before copying our stdin over to sqlplus's stdin
    if(pipe(fds) == -1) {
        print_stacktrace();
        PERROR("pipe()");
        return 1;
    }
    snprintf(sqlplus_program, sizeof(sqlplus_program), "%s/bin/sqlplus /NOLOG", oraclehome);
    if((sqlplus_args=make_args(sqlplus_program)) == NULL) {
        print_stacktrace();
        fprintf(stderr, "Could not make sqlplus program argument array\n");
        fflush(stderr);
        exit(1);
    }
    if((sqlplus_pid=fork()) < 0) {
        print_stacktrace();
        PERROR("fork()");
        return 1;
    }
    if(sqlplus_pid > 0) {
        // parent
        close(fds[0]);                // close read side of pipe
        dup2(fds[1], fileno(stdout)); // wire up stdout (fd 1) to write side of the pipe (fds[1])
        close(fds[1]);
        char *spool_begin="spool ";
        char *spool_session_log=SQLPLUS_SESSION_LOG;
        char *spool_end=";\n";
        char *define_off="set define off;\n";
        char *connect_start="connect ";
        char *connect_end="\n";
        char *define_on="set define on;\n";
        connect_str=make_connect_str(connect_template, ora_username, ora_pw);
        if(debug) {
            fprintf(stderr, "Logging sqlplus session to: %s\n", SQLPLUS_SESSION_LOG);
            write(fileno(stdout), spool_begin, strlen(spool_begin));
            write(fileno(stdout), spool_session_log, strlen(spool_session_log));
            write(fileno(stdout), spool_end, strlen(spool_end));
            fprintf(stderr, "Sending to sqlplus (without the brackets): [connect %s]\n", connect_str);
        }
        write(fileno(stdout), define_off, strlen(define_off));
        write(fileno(stdout), connect_start, strlen(connect_start));
        write(fileno(stdout), connect_str, strlen(connect_str));
        write(fileno(stdout), connect_end, strlen(connect_end));
        write(fileno(stdout), define_on, strlen(define_on));
        fflush(stdout);
        // zero username/password/connect string to prevent someone from reading them from memory
        memset(ora_username, 0, sizeof(ora_username));
        memset(ora_pw, 0, sizeof(ora_username));
        char *pcs;
        for(pcs=connect_str; *pcs != '\0'; pcs++)
            *pcs = '\0';
        // copy stdin to the write side of pipe (this read(2) will block)
        while((count=read(fileno(stdin), buf, BUF_MAX)) > 0) {
            write(fileno(stdout), buf, count);
        }
        status=0;
        if(waitpid(sqlplus_pid, &status, 0) == -1) {
            print_stacktrace();
            fprintf(stderr, "Failed to wait on sqlplus program\n");
            PERROR("waitpid(sqlplus_pid, &status, 0)");
            fflush(stderr);
        } else if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            fprintf(stderr, "Failed to execute sqlplus program (it returned %d)\n", WEXITSTATUS(status));
            fflush(stderr);
            exit(WEXITSTATUS(status));
        }
        return 0; 
    } else {
        // child
        dup2(fds[0], fileno(stdin)); // wire up fd 0 to read side of pipe
        close(fds[0]);
        close(fds[1]);

        setenv("ORACLE_HOME", oraclehome, 1);
        if(debug)
            fprintf(stderr, "Exec: %s\n", sqlplus_program);
        if(execv(sqlplus_args[0], sqlplus_args) == -1) {
            snprintf(logbuf, sizeof(logbuf), "Unable to execute \"%s\"", sqlplus_args[0]);
            PERROR(logbuf);
            return 1;
        }
        // if we get here then exec failed. inform parent
        fprintf(stderr, "exec failed\n");
        return 1;
    }
}
