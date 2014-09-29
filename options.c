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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_sqlplus.h"

static struct option long_options[]={
      {"connectstring"  , required_argument, NULL, 'c'},
      {"debug"          , no_argument      , NULL, 'd'},
      {"help"           , no_argument      , NULL, 'h'},
      {"oraclehome"     , required_argument, NULL, 'o'},
      {"passwordprogram", required_argument, NULL, 'p'},
      {"sqlplusargs"    , required_argument, NULL, 'a'},
      {"usernameprogram", required_argument, NULL, 'u'},
      {NULL             , 0,                 NULL,  0 }
};

void usage(char *argv0) {
    printf("usage: %s -c connectstring -o oraclehome -u usernameprogram -p pwprogram\n", argv0);
    printf("Mandatory:\n");
    printf(" -c,--connectstring     Connect string, passed to connect command for login in sqlplus\n");
    printf("                        Two variables are available: {{username}} and {{password}}, which\n");
    printf("                        will be replaced with the result of running\n");
    printf("                        usernameprogram (-u) and passwordprogram (-p)\n");
    printf(" examples:\n");
    printf(" -c '{{username}}/\"{{password}}\"@\"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=oradb01.initech.com)(PORT=1521))(CONNECT_DATA=(SID=oradb01)))\"'\n");
    printf(" -c 'sys/\"{{password}}\"@\"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=oradb01.initech.com)(PORT=1521))(CONNECT_DATA=(SID=oradb01)))\" AS SYSDBA'\n");
    printf(" -c '{{username}}/\"{{password}}\"@\"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=oradb01.initech.com)(PORT=1521))(CONNECT_DATA=(SERVICE_NAME=pluggable1)))\"'\n");
    printf(" -o,--oraclehome        Path to Oracle home (same as ORACLE_HOME environment variable)\n");
    printf("                        This program will execute ORACLE_HOME/bin/sqlplus\n");
    printf(" -u,--usernameprogram   Path and arguments to program that will return Oracle database username\n");
    printf(" -p,--passwordprogram   Path and arguments to program that will return Oracle database password\n");
    printf("                        NOTE: username and password programs are passed to execv(), so\n");
    printf("                              things like pipes as well as single and double quotes\n");
    printf("                              are not supported.\n");
    printf("                              Just provide a single script or program that will return\n");
    printf("                              the uname/password\n");
    printf(" examples:\n");
    printf(" -u /usr/local/bin/get_oracle_username\n");
    printf("\n");
    printf("Optional:\n");
    printf(" -a,--sqlplusargs       Additional arguments to pass to the sqlplus program\n");
    printf(" -d,--debug             Print debug messages\n");
    printf(" -h,--help              This help message\n");
    printf("Report bugs to <ryan@rchapman.org>\n");
}

void parse_args(int argc, char *argv[]) {
    bool show_usage_and_exit=false;
    int c;
    int option_index=0;
    
    debug=false;

    while((c=getopt_long(argc, argv, "a:c:dho:p:u:", long_options, &option_index)) != -1) {
        switch(c) {
            case 0: // flag. do nothing.
                break;
            case 'a':
                if(optarg == NULL)
                    sqlplusargs[0]='\0';
                else
                    strncpy(sqlplusargs, optarg, sizeof(sqlplusargs));
                break;
            case 'c':
                if(optarg == NULL)
                    connect_template[0]='\0';
                else
                    strncpy(connect_template, optarg, sizeof(connect_template));
                break;
            case 'd':
                debug=true;
                break;
            case 'h':
                usage(argv[0]);
                exit(1);
                break;
            case 'o':
                if(optarg == NULL)
                    oraclehome[0]='\0';
                else
                    strncpy(oraclehome, optarg, sizeof(oraclehome));
                break;
            case 'p':
                if(optarg == NULL)
                    pw_program[0]='\0';
                else
                    strncpy(pw_program, optarg, sizeof(pw_program));
                break;
            case 'u':
                if(optarg == NULL)
                    username_program[0]='\0';
                else
                    strncpy(username_program, optarg, sizeof(username_program));
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    if(*connect_template == '\0') {
        fprintf(stderr, "Usage error: You must specify connect string (-c)\n");
        show_usage_and_exit=true;
    }

    if(*oraclehome == '\0') {
        fprintf(stderr, "Usage error: You must specify Oracle home (-o)\n");
        show_usage_and_exit=true;
    }

    if(*pw_program == '\0') {
        fprintf(stderr, "Usage error: You must specify a password program (-p)\n");
        show_usage_and_exit=true;
    }

    if(*username_program == '\0') {
        fprintf(stderr, "Usage error: You must specify a username program (-u)\n");
        show_usage_and_exit=true;
    }

    if(show_usage_and_exit) {
        usage(argv[0]);
        exit(1);
    }
}
