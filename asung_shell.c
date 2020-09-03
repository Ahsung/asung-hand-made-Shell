#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <signal.h>
#define arg_MAX 100
#define str_MAX 100
#define command_token_MAX 20
#define token_MAX 20
#define pipe_MAX 20

/*
File Descriptor info for PIPE, Redirection
execfile inlcuding argument
 */
typedef struct pipunit{
    int stdinfd;
    int stdoutfd;
    int stderrfd;
    char **exec;
} pipunit;

char commandLine[arg_MAX][str_MAX]; // command in One Line 
char command[arg_MAX][str_MAX]; // One command  including Redirection, PIPE
char *command_token[command_token_MAX][token_MAX]; // one execfile including argument , div redirection or pipe, ";" , "&"
char *redir[command_token_MAX]; // redirection token string Pointer
int bgflag;

int pid_list[pipe_MAX];
int pidSize;
int fd_list[arg_MAX]; // file descriptor_ list
int fdSize;

pipunit punit_list[pipe_MAX];  // Pipe Unit_list, 
int pipeSize = 0;

int child_process_num = 0;


// Set Structure for Command
int inputCommand(const char *argv);
int copyTokenStr(char *dst, char *src, int src_idx, int *suc_flag);
int set_Command_Token(int command_len);
int set_pipe_list(int redir_size);

// Run
int forkexec(pipunit punit);
void except_allClose(int std_in,int std_out,int std_err);
void runCommand(int size);
void runCommandLine(int size);

// etc..
int check_redirection(const char *token);
void init_gb();
void init_pipunit(pipunit *p,char ** exec_Name);

void sig_child(int signo){
	pid_t pid; int status;
	pid = wait(&status);
	if(pid >= 0)
		printf("[%d] Done\n",pid);
}

char *targv[2];
int cflag = 0;
void sig_int(int signo){
		cflag = 1;
}

/*---------------------Debug func--------------------*/
void show_pipe_list_info();
void show_command_token(int token_size);
/*---------------------------------------------------*/


int main(int argc,char *argv[])
{
    int i;
    int size;

	char tmpp[str_MAX];
	strcpy(tmpp,argv[0]);
	targv[0] = tmpp;
	targv[1] = NULL;
	
	signal(SIGCHLD,sig_child);
	
	signal(SIGINT,sig_int);
	
    // Run Shell using argument
    // Only use -c option
    if(argc > 1){
        if(strcmp(argv[1],"-c") != 0){
            fprintf(stderr,"wrong option!\n");
            exit(1);
        }
        else if(argc < 3){
            fprintf(stderr,"requires an argument!\n");
            exit(1);
        }

        init_gb();

        size = inputCommand(argv[2]);
        if (size <= 0)
            return 0;

        runCommandLine(size);
        exit(0);
    }
    // No argument Run
	while (1)
    {
        init_gb();
        char buf[100];getcwd(buf,100);
        printf("%s: %s $ ",getenv("USER"),buf);
		
		size = inputCommand(NULL);

		if(cflag){
			cflag = 0;
			continue;
		}

		if (size < 0)
        {
            printf("\n");
            return 0;
        }
        else if (size == 0)
            continue;
        runCommandLine(size);
    }
}


//-----------------------------------------------------------Function Definition-----------------------------------------------------------


// div Command line  to each Command (";")  AND RUN
void runCommandLine(int size){
    int i;
    int csize = 0;
    for(i = 0; i < size; i++){
        if( strcmp(commandLine[i],";") == 0){
              runCommand(csize);
            csize =0;
          }
        else
            strcpy(command[csize++],commandLine[i]);
        }
    if(csize)
        runCommand(csize);
}

/* 
One command exec
include pipe,redirection
*/
void runCommand(int size)
{
    init_gb();
    int i;

    //change dir
    if (strcmp(command[0], "cd") == 0)
    {
        if (size == 1)
        {
            if (chdir(getenv("HOME")) < 0)
                fprintf(stderr, "cd fail\n");
            return;
        }
        if (chdir(command[1]) < 0)
            fprintf(stderr, "cd fail\n");
        return;
    }

    // Command
    int token_size = set_Command_Token(size);
    if (token_size < 0)
        return;
    //if back ground exec
    bgflag = (int)(strcmp(command[size - 1], "&") == 0);

    int rsize = token_size - 1; //redirection Toekn Num

    if (set_pipe_list(rsize) < 0) 
        return;

    int cpid; // if back ground exce,, printf pid
    for (i = 0; i < pipeSize; i++)
    {
        if ((cpid = forkexec(punit_list[i])) < 0)
            break;
    }

    // parent close all fd except STD .. for PIPE 
    except_allClose(STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
    if (bgflag)
    {
        printf("[%d] %d\n", ++child_process_num, cpid);
        return;
    }
    for (i = 0; i < pidSize; i++)
        waitpid(pid_list[i], NULL, 0);
}


/*
child process Fork
get fd info from punit
Change File Descriptor for redirection or pipe
and exec
return pid
*/
int forkexec(pipunit punit)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        fprintf(stderr,"fork fail\n");
        return -1;
    }

    pid_list[pidSize++] = pid;

    //child
    if (pid == 0)
    {
		signal(SIGINT,SIG_DFL);
        //Change FD
        except_allClose(punit.stdinfd,punit.stdoutfd,punit.stderrfd);
        if(punit.stdinfd != STDIN_FILENO ) dup2(punit.stdinfd,STDIN_FILENO);
        if(punit.stdoutfd != STDOUT_FILENO ) dup2(punit.stdoutfd,STDOUT_FILENO);
        if(punit.stderrfd != STDERR_FILENO ) dup2(punit.stderrfd,STDERR_FILENO);

        if (execvp(punit.exec[0],punit.exec) < 0)
        { // if fali, return child..
            fprintf(stderr,"exec fail\n");
            exit(1);
        }
    }
    return pid;
}

/*
close all FD
except Process will use fd
*/
void except_allClose(int std_in,int std_out,int std_err){
    int i;
    for (i=0; i < fdSize; i++){
        if( fd_list[i] != std_in && fd_list[i] != std_out && fd_list[i] != std_err){
            close(fd_list[i]);
        }
    }
}


/*
Set Command Token, redirection Token(<, >,|)
Command Token is  One exec file including argument
Set command_token pointer to command string
and Only use last Operation Token '&'
return Command token size
*/
int set_Command_Token(int command_len)
{
    int token_num = 0;
    int cur_word_token_idx = 0;
    int i;
    // Setting Pointer
    for (i = 0; i < command_len; i++)
    {
        if (!check_redirection(command[i]))
            command_token[token_num][cur_word_token_idx++] = command[i];
        else
        {
            command_token[token_num][cur_word_token_idx] = NULL;
            redir[token_num++] = command[i];
            cur_word_token_idx = 0;
        }
    }

    //if exist back ground token
    if (check_redirection(command[command_len - 1]) == 5)
        token_num--;
    else if(check_redirection(command[command_len - 1])){
        fprintf(stderr,"Last Operation, can use only \'&\'\n");
        return -1;
    }
    else
        command_token[token_num][cur_word_token_idx] = NULL;

    return token_num + 1;
}


/*
Set File Descriptor Info for Pipe, redirection
Accroding redirection token type
and Set execfile
in One Command
*/
int set_pipe_list(int redir_size)
{
    init_pipunit(&punit_list[pipeSize++],command_token[0]);
    
    int i;
    int fd;
    int pfd[2]; //pipe fd

    for( i =0; i < redir_size; i++){

        switch (check_redirection(redir[i]))
        {
        // < , Input file
        case 1:
            if((fd = open(command_token[i+1][0],O_RDONLY))  < 0){
                fprintf(stderr,"open (R) fail\n"); return -1;
            }
            fd_list[fdSize++] = fd;
            punit_list[pipeSize-1].stdinfd = fd;
            break;
        // >, Output file
        case 2:
            if((fd = creat(command_token[i+1][0],0664))  < 0){
                fprintf(stderr,"open (W) fail\n"); return -1;
            }
            fd_list[fdSize++] = fd;
            punit_list[pipeSize-1].stdoutfd = fd;
            break;
        // 2>, Output Err file
        case 3:
            if((fd = creat(command_token[i+1][0],0664))  < 0){
                fprintf(stderr,"open (W,e) fail\n"); return -1;
            }
            fd_list[fdSize++] = fd;
            punit_list[pipeSize-1].stderrfd = fd;
            break;
        // PIPE
        case 4:
            init_pipunit(&punit_list[pipeSize++],command_token[i+1]);
            if (pipe(pfd) < 0){
                fprintf(stderr, "pipe fail\n");
                return -1;
            }
            fd_list[fdSize++] = pfd[0];
            fd_list[fdSize++] = pfd[1];
            punit_list[pipeSize-2].stdoutfd = pfd[1];
            punit_list[pipeSize-1].stdinfd = pfd[0];
            break;
        }
    }
    return 0;
}


void init_gb(){
    pipeSize = 0;
    fd_list[0] = STDIN_FILENO;
    fd_list[1] = STDOUT_FILENO;
    fd_list[2] = STDERR_FILENO;
    fdSize = 3;
    pidSize = 0;
}

// init STD
void init_pipunit(pipunit *p,char ** exec_Name){
    p->exec= exec_Name;
    p->stdinfd = STDIN_FILENO;
    p->stdoutfd = STDOUT_FILENO;
    p->stderrfd = STDERR_FILENO;
}

int check_redirection(const char *token)
{
    if (strcmp(token, "<") == 0 || strcmp(token, "0<") == 0)
        return 1;
    if (strcmp(token, ">") == 0 || strcmp(token, "1>") == 0)
        return 2;
    if (strcmp(token, "2>") == 0)
        return 3;
    if (strcmp(token, "|") == 0)
        return 4;
    if (strcmp(token, "&") == 0)
        return 5;

    return 0;
}

/*
Input Command
return Command argc size
if EOF return -1
if argv is NULL, recv STDIN
*/
int inputCommand(const char *argv)
{
    int size = 0;
    char tmp;
    const int bufsize = 1500;
    int next_idx = 0;
    char buf[bufsize];
    char *flag;
    if(!argv)
        flag = fgets(buf, bufsize, stdin);
    else
        strcpy(buf,argv);
    
    int buf_len = strlen(buf);
    int suc_flag = 0;
    //if fail,, return -1
    if (!flag)
        return -1;
    else if (buf[0] == '\n')
        return 0;
    else
    {
        while (1)
        {   //parsing
            next_idx = copyTokenStr(commandLine[size], buf, next_idx, &suc_flag) + 1;
            if (suc_flag)
                size++;
            //END
            if (next_idx >= buf_len - 1)
                return size;
        }
    }
}

/*
copy str until meet blank
return last Char idx
if only blank, suc_flag = false
*/
int copyTokenStr(char *dst, char *src, int src_idx, int *suc_flag)
{
    *suc_flag = 0;
    int didx = 0;
    // Skip blank
    while (src[src_idx] == ' ')
        src_idx++;

    // For Possibility using Redirection Number
    if (src[src_idx] >= '0' && src[src_idx] <= '3')
    {
        *suc_flag = 1;
        dst[didx++] = src[src_idx++];
    }

    // Special Token, Can not distingush by Blank
    // so when appear, Tokenize right now
    if (src[src_idx] == '&' || src[src_idx] == '|' || src[src_idx] == '<' || src[src_idx] == '>'|| src[src_idx] == ';')
    {
        *suc_flag = 1;
        dst[didx++] = src[src_idx];
        dst[didx] = '\0';
        return src_idx;
    }

    // copy word
    while (src[src_idx] != ' ' && src[src_idx] != '&' && src[src_idx] != '|' && src[src_idx] != '<' && src[src_idx] != '>'&& src[src_idx] != ';')
    {
        if (src[src_idx] == '\0')
        {
            dst[didx] = '\0';
            return src_idx - 1;
        }
        if (src[src_idx] == '\n')
        {
            dst[didx] = '\0';
            return src_idx - 1;
        }

        *suc_flag = 1;
        dst[didx++] = src[src_idx++];
    }

    dst[didx] = '\0';
    return src_idx - 1;
}


//debug func
void show_pipe_list_info(){
    int i;
    for(i=0; i < pipeSize; i++ ){
        fprintf(stderr,"i:%d o:%d e:%d  : ",punit_list[i].stdinfd,punit_list[i].stdoutfd,punit_list[i].stderrfd);
        int j =0;
        while(punit_list[i].exec[j]!=NULL){
            fprintf(stderr,"%s ",punit_list[i].exec[j]);
            j++;
        }
        fprintf(stderr,"\n");
    }
}

//debug func
void show_command_token(int token_size){
    int i;
    for (i = 0; i < token_size; i++)
        {
            fprintf(stderr,"No.%d command tokens!\n", i);
            int j = 0;
            while (command_token[i][j] != NULL)
            {
                fprintf(stderr,"%s ", command_token[i][j]);
                j++;
            }
            fprintf(stderr,"\n");
        }
}
