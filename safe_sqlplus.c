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
        pid=wait3(&status, WNOHANG, (struct rusage *)NULL);
        if(pid == 0) {
            return;
        } else if(pid == -1) {
            return;
        } else {
            // if child dies with non-zero result, parent should die immediately.
            if(WEXITSTATUS(status) != 0)
                exit(WEXITSTATUS(status));
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
        fprintf(stderr, "arg=%s\n", arg);
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
    char connect_str[CONNECT_MAX];

    if(signal(SIGCHLD, sighandle_sigchld) == SIG_ERR ||
       signal(SIGSEGV, sighandle_sigsegv) == SIG_ERR ||
       signal(SIGFPE,  sighandle_sigfpe)  == SIG_ERR ||
       signal(SIGILL,  sighandle_sigill)  == SIG_ERR) {
        perror("could not set up signal handler");
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
            fprintf(stderr, "Could not get Oracle sqlplus username");
            return 1;
        }
        if(ora_username[strlen(ora_username)-1] == '\n') {
            ora_username[strlen(ora_username)-1]='\0';
        }
        fprintf(stderr, "Got oracle username=\"%s\"\n", ora_username);
        waitpid(username_pid, &status, 0);
        if(WEXITSTATUS(status) != 0)
            exit(WEXITSTATUS(status));
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
            perror(logbuf);
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
            fprintf(stderr, "Could not get Oracle sqlplus password");
            return 1;
        }
        if(ora_pw[strlen(ora_pw)-1] == '\n') {
            ora_pw[strlen(ora_pw)-1]='\0';
        }
        fprintf(stderr, "Got oracle password=\"%s\"\n", ora_pw);
        waitpid(pw_pid, 0, 0);
        if(WEXITSTATUS(status) != 0)
            exit(WEXITSTATUS(status));
        // continue on to run sqlplus
    } else {
        // child
        close(fds[0]);  // read side of pipe
        dup2(fds[1], fileno(stdout));  // wire up stdout (fd1) to write side of the pipe (fds[1])
        if(debug) {
            fprintf(stderr, "Exec password program: %s", pw_args[0]);
            char **p;
            p=(char **)pw_args;
            while(*p != NULL) {
                fprintf(stderr, " %s", *p);
                p++;
            }
            fprintf(stderr, "\n");
        }
        if(execv(pw_args[0], pw_args) == -1) {
            snprintf(logbuf, sizeof(logbuf), "Unable to execute \"%s\"", pw_args[0]);
            perror(logbuf);
            exit(1);
        }
        return 0;
    }

    // fork/exec sqlplus, but inject the connect command before copying our stdin over to sqlplus's stdin
    if(pipe(fds) == -1) {
        perror("pipe()");
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
        perror("fork()");
        return 1;
    }
    if(sqlplus_pid > 0) {
        // parent
        close(fds[0]);                // close read side of pipe
        dup2(fds[1], fileno(stdout)); // wire up stdout (fd 1) to write side of the pipe (fds[1])
        close(fds[1]);
        char *define_off_str="set define off;\n";
        // TODO: for debugging login failures: char *define_off_str="set define off;\nspool /tmp/spool.log;\n";
        char *define_on_str="set define on;\n";
        memset(connect_str, 0, CONNECT_MAX);
        snprintf(connect_str, 
                 CONNECT_MAX,
                 "connect system/\"%s\"@\"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=%s)(PORT=%s))(CONNECT_DATA=(%s)))\"\n", ora_pw, host, port, connect_data);
        connect_str[CONNECT_MAX-1]='\0';
        if(debug)
            fprintf(stderr, "Sending to sqlplus: \"%s\"\n", connect_str);
        write(fileno(stdout), define_off_str, strlen(define_off_str));
        write(fileno(stdout), connect_str, strlen(connect_str));
        write(fileno(stdout), define_on_str, strlen(define_on_str));
        fflush(stdout);
        // copy stdin to the write side of pipe (this read(2) will block)
        while((count=read(fileno(stdin), buf, BUF_MAX)) > 0) {
            write(fileno(stdout), buf, count);
        }
        waitpid(sqlplus_pid, &status, 0);
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
            perror(logbuf);
            return 1;
        }
        // if we get here then exec failed. inform parent
        fprintf(stderr, "exec failed\n");
        return 1;
    }
}
