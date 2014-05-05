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
#include <stdbool.h>

#define PERROR(s)  fprintf(stderr, "Error at %s:%d:%s(): ", __FILE__, __LINE__, __FUNCTION__); perror(s);

#define BUF_MAX              4096
#define LOGBUF_MAX           4096
#define PW_MAX               512
#define USERNAME_MAX         512
#define USERNAME_PROGRAM_MAX 4096
#define PW_PROGRAM_MAX       4096
#define CONNECT_MAX          1024
#define ORACLEHOME_MAX       2048
#define SQLPLUS_MAX          4096
#define CONNECTTEMPLATE_MAX  8192
#define SQLPLUS_SESSION_LOG  "./sqlplus_session.log"

bool debug;
char connect_template[CONNECTTEMPLATE_MAX];
char oraclehome[ORACLEHOME_MAX];
char pw_program[PW_PROGRAM_MAX];
char username_program[USERNAME_PROGRAM_MAX];

void usage(char *argv0);
void parse_args(int argc, char *argv[]);
void print_stacktrace(void);

