[![Build Status](https://travis-ci.org/ryanchapman/safe_sqlplus.png)](https://travis-ci.org/ryanchapman/safe_sqlplus)

# safe_sqlplus

safe_sqlplus is a wrapper around the sqlplus program that comes with the Oracle database.
It provides a way to execute sqlplus and make an initial connection to the database 
without having to specify the username and password on the command line.

## Travis-CI

Build status can be found at http://travis-ci.org/ryanchapman/safe_sqlplus

## Building

Simply run ``make``.  If you find build errors on your operating system, please open 
an issue on GitHub.

## Details

It is possible to connect to an Oracle database by running the sqlplus command and providing
credentials on the command line, like so:  

    su - oracle -c '$ORACLE_HOME/bin/sqlplus system/"psx0/6VlZ"@"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=oradb01.initech.com)(PORT=1521))(CONNECT_DATA=(SID=oradb01)))'

Unfortunately, this is not secure, as users on the system can run "ps auxwwe" and view the 
username and password in clear text:

    ryan@oradb01:~$ ps aux | grep sqlplus
    root     21698  0.0  0.0 163800  1932 pts/0    S    23:18   0:00 su - oracle -c $ORACLE_HOME/bin/sqlplus system/"psx0/6VlZ"@"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=oradb01.initech.com)(PORT=1521))(CONNECT_DATA=(SID=oradb01)))"
    oracle   21699  0.0  0.0  90692 14584 ?        Ss   23:18   0:00 /apps/oracle/product/12.1.0/bin/sqlplus

Instead, safe_sqlplus executes commands to find the Oracle database username and password.
For example, given the invocation:

    su - oracle -c '/usr/local/bin/safe_sqlplus -H oradb01.initech.com -P 1521 -u /usr/local/bin/get_ora_username -p /usr/local/bin/get_ora_pw -o $ORACLE_HOME -c SID=oradb01'

safe_sqlplus will set up a pipe(2) and then fork and execute /usr/local/bin/get_ora_username to determine the username.  get_ora_username might look like:

    #!/bin/bash -

    # Get Oracle database password from remote server
    echo $(curl -qs http://rpc01.initech.com/api/getdbuser?token=adc83b19e793491b1c6ea0fd8b46cd9f32e592fc)

safe_sqlplus then forks and executes /usr/local/bin/get_ora_password to get the Oracle database password.

It finally forks and executes $ORACLE_HOME/bin/sqlplus and prints this on the sqlplus processes standard input:

    set define off;
    connect system/"psx0/6VlZ"@"(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=oradb01.initech.com)(PORT=1521))(CONNECT_DATA=(SID=oradb01)))"
    set define on;

and then copies standard input from the safe_sqlplus process to the standard input of the sqlplus process.

    
    
    Diagram of pipes and streams

       ______________________________________________________________________________ 
      |                                                                              |
      |     _____________________________________________________________________    |
      |    |                                                                     |   |
      |    |     _________________________________                               |   |
      |    |    |                                 |                              |   |
      |    |    |       +-------------------+     |    +-------------------+     |   |
      |    |    |       |          |   fd 2 |     |    |          |   fd 2 |     |   |
     \ /  \ /  \ /      |          | stderr |-->--+    |          | stderr |-->--+   |
    +-------------+     |          |________|          |          |________|         |
    |    pts/N    |     |                   |          |                   |         |
    +-------------+     |     safe_sqlplus  |          |      sqlplus      |         |
           |            |_______    ________|          |_______    ________|         |
           |            | fd 0  |  |   fd 1 |          | fd 0  |  |   fd 1 |         |
           +----------->| stdin |  | stdout |-->------>| stdin |  | stdout |-->------+
                        +-------------------+          +-------------------+
    
    

### Examples

Connect to container
    ``/usr/local/bin/safe_sqlplus -H oradb01.initech.com -P 1521 -u /usr/local/bin/get_ora_username -p /usr/local/bin/get_ora_pw -o /apps/oracle/12c -c SID=oradb01``

Connect to pluggable database
    ``/usr/local/bin/safe_sqlplus -H oradb01.initech.com -P 1521 -u /usr/local/bin/get_ora_username -p /usr/local/bin/get_ora_pw -o /apps/oracle/12c -c SERVICE_NAME=pluggable1.initech.com``


-Ryan A. Chapman
 Sat May  3 22:46:30 MDT 2014
