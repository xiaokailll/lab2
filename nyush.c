#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#define N_pipe 100

typedef struct link{
    char **command;
    int input_fd;
    char* input_file;
    int output_fd;
    char* output_file;
    struct link* next;
}Link;
Link* head = NULL;

typedef struct jobs{
    int num;
    int check;
    char* command_line;
    int pid_num;
    int pid[N_pipe];
    struct jobs* next;
}Jobs;
Jobs* head_jobs = (Jobs*)malloc(sizeof(Jobs));
Jobs* tail_jobs = head_jobs;

typedef struct working_jobs{
    int pid;
    struct working_jobs* next;
}W_jobs;
W_jobs* w_job_head = NULL;


void my_system(Link* node){
    if(!node)return;
    int pid_child, wstatus, fp;
    pid_child = fork();
    if(pid_child == 0) {
        signal(SIGINT,SIG_IGN);
        if(node->input_fd){
            if(node->input_fd==-1){ //file
                fp = open(node->input_file, O_RDWR);
                if(fp==-1){
                    fprintf(stderr, "Error: invalid file\n");
                    exit(-1);}
                dup2(fp, STDIN_FILENO);
            }
            else{ //pipe
                close(node->input_fd+1);
                dup2(node->input_fd, STDIN_FILENO);
            }
        }
        if(node->output_fd){
            if(node->output_fd==-1){
                fp = open(node->output_file, O_WRONLY|O_CREAT|O_TRUNC, 00600);
                if(fp==-1){
                    fprintf(stderr, "Error: invalid file\n");
                    exit(-1);}
                dup2(fp, STDOUT_FILENO);
            }
            else if(node->output_fd==-2){
                fp = open(node->output_file, O_RDWR|O_CREAT|O_APPEND, 00600);
                if(fp==-1){
                    fprintf(stderr, "Error: invalid file\n");
                    exit(-1);}
                dup2(fp, STDOUT_FILENO);
            }
            else {
                close(node->input_fd-1);
                dup2(node->output_fd, STDOUT_FILENO);
            }
        }
        

        char* prog_name = (char*)malloc(1001*sizeof(char));
        if(node->command[0][0]==0x2f||node->command[0][0]==0x2e){
            execv(node->command[0], node->command);
            fprintf(stderr, "Error: invalid progarm\n");
            exit(-1);
        }
        char prog_root[1001] = {0};
        strcpy(prog_root, "/bin/");
        strcat(prog_root, node->command[0]);
        execv(prog_root, node->command);
        strcpy(prog_root, "/usr/bin/");
        strcat(prog_root, node->command[0]);
        execv(prog_name, node->command);
        fprintf(stderr, "Error: invalid progarm\n");
        exit(-1);
    }
    else{
        W_jobs* new_node=(W_jobs*)malloc(sizeof(W_jobs));
        new_node->pid = pid_child;
        new_node->next = w_job_head;
        w_job_head = new_node;

        my_system(node->next);
        
        if(node->input_fd>0)close(node->input_fd);
        if(node->output_fd>0)close(node->output_fd);
        waitpid(pid_child, &wstatus, WUNTRACED);
        if (WIFSTOPPED(wstatus)){  // child be stopped
            if(tail_jobs->check){  //check =1: finish; check =0: still open
                tail_jobs->next = (Jobs*)malloc(sizeof(Jobs));
                tail_jobs->next->num = tail_jobs->num +1 ;
                tail_jobs = tail_jobs->next;
                tail_jobs->check = 0;
                tail_jobs->pid_num = 1;
                tail_jobs->pid[0] = pid_child;
                tail_jobs->next = NULL;
            }
            else{
                tail_jobs->pid[tail_jobs->pid_num] = pid_child;
                tail_jobs->pid_num += 1;
            }
        }
    }
    return;
}


void command_parser(char* command_line, int is_first_command, int is_last_command, int input_fd, int output_fd){ //no pipe in this function
    int wstatus;
    if(is_first_command)head=NULL; // can be improve, free the space
    char command_line_new[1001];
    memcpy(command_line_new,command_line, 1001);
    char* command;
    char* rest_command = command_line_new;
    command = strtok_r(rest_command, " ", &rest_command);
    if(!strcmp(command, "exit")){
        if(!is_first_command||!is_last_command){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        if(strtok_r(rest_command, " ", &rest_command)){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        if(head_jobs->next){
            fprintf(stderr, "Error: there are suspended jobs\n");
            return;
        }
        printf("exit\n");
        int pid = getpid();
        kill(pid, SIGKILL);
    }
    else if(!strcmp(command, "cd")){
        if(!is_first_command||!is_last_command){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        char *path = strtok_r(rest_command, " ", &rest_command);
        if(strtok_r(rest_command, " ", &rest_command)){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        char working_directory[1024] = {0};
        getcwd(working_directory, sizeof(working_directory));
        int check;
        check = chdir(path);
        if(check == -1){
            fprintf(stderr, "Error: invalid directory\n");
            return;
        return;
        }
    }
    else if(!strcmp(command, "jobs")){
        if(!is_first_command||!is_last_command){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        if(strtok_r(rest_command, " ", &rest_command)){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        Jobs* probe = head_jobs->next;
        while(probe){
            printf("[%d] Stopped\t%s\n", probe->num, probe->command_line);
            probe = probe->next;
        }
        return;
    }
    else if(!strcmp(command, "fg")){
        if(!is_first_command||!is_last_command){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        command = strtok_r(rest_command, " ", &rest_command);
        int job_num;
        if(command==NULL&&tail_jobs)job_num = tail_jobs->num;
        else job_num = atof(command);
        if(job_num==0){
            fprintf(stderr, "Error: invalid job\n");
            return;
        }
        if(strtok_r(rest_command, " ", &rest_command)){
            fprintf(stderr, "Error: invalid command\n");
            return;
        }
        Jobs* probe = head_jobs->next;
        while(probe){
            if(probe->num == job_num){
                printf("%s\n", probe->command_line);
                for(int i=0;i<probe->pid_num;i++){
                    W_jobs* new_node=(W_jobs*)malloc(sizeof(W_jobs));
                    new_node->pid = probe->pid[i];
                    new_node->next = w_job_head;
                    w_job_head = new_node;
                    kill(probe->pid[i], SIGCONT); 
                }
                for(int i=0;i<probe->pid_num;i++){
                    waitpid(probe->pid[i], &wstatus, WUNTRACED);
                }
                if (WIFSTOPPED(wstatus)) {
                    printf("[%d] Stopped\t%s\n", probe->num, probe->command_line);
                    return;
                }
                else{ // clean up
                    Jobs* temp = head_jobs;
                    while(temp->next){
                        if(temp->next->num == job_num){
                            if(tail_jobs == probe) tail_jobs=temp;
                            temp->next = probe->next;
                            free(probe);
                            return;
                        }
                        else temp = temp->next;
                    }
                }
                return;
            }
            else probe = probe->next;
        }
        fprintf(stderr, "Error: invalid job\n");
        return;
    }

    Link *one_command=(Link*)malloc(sizeof(Link));
    one_command->input_fd = 0;
    one_command->output_fd = 0;
    one_command->input_file = (char*)malloc(1001*sizeof(char));
    one_command->output_file = (char*)malloc(1001*sizeof(char));
    if(input_fd!=-1) one_command->input_fd = input_fd;
    if(output_fd!=-1) one_command->output_fd = output_fd;
    char** argv;
    one_command->command = (char**)malloc(sizeof(char*)*1001);
    
    char redirect_path[1001] = {0};
    int i = 0;
    while (command){
        if(!strcmp(command, "<")||!strcmp(command, ">")||!strcmp(command, ">>")){
            if(!strcmp(command, "<")){
                one_command->input_fd = -1;
                if(!is_first_command){
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
                command = strtok_r(rest_command, " ", &rest_command);
                if(command == NULL){
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
                strcpy(one_command->input_file, command);
                command = strtok_r(rest_command, " ", &rest_command);
                if(command != NULL){ 
                    if(!strcmp(command, ">")){
                        one_command->output_fd = -1;
                        if(!is_last_command){
                            fprintf(stderr, "Error: invalid command\n");
                            return;}
                        command = strtok_r(rest_command, " ", &rest_command);
                        if(command == NULL){
                            fprintf(stderr, "Error: invalid command\n");
                            return;}
                        strcpy(one_command->output_file, command);
                        command = strtok_r(rest_command, " ", &rest_command);
                        if(command != NULL){ 
                            fprintf(stderr, "Error: invalid command\n");
                            return;}
                    }
                    else if(!strcmp(command, ">>")){
                        one_command->output_fd = -2; 
                        if(!is_last_command){
                            fprintf(stderr, "Error: invalid command\n");
                            return;}
                        command = strtok_r(rest_command, " ", &rest_command);
                        if(command == NULL){
                            fprintf(stderr, "Error: invalid command\n");
                            return;}
                        strcpy(one_command->output_file, command);
                        command = strtok_r(rest_command, " ", &rest_command);
                        if(command != NULL){ 
                            fprintf(stderr, "Error: invalid command\n");
                            return;}
                    }
                    else{
                        fprintf(stderr, "Error: invalid command\n");
                        return;}
                }    
            }
            else if(!strcmp(command, ">")){
                one_command->output_fd = -1;
                if(!is_last_command){
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
                command = strtok_r(rest_command, " ", &rest_command);
                if(command == NULL){
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
                strcpy(one_command->output_file, command);
                command = strtok_r(rest_command, " ", &rest_command);
                if(command != NULL){ 
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
            }
            else{
                one_command->output_fd = -2; 
                if(!is_last_command){
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
                command = strtok_r(rest_command, " ", &rest_command);
                if(command == NULL){
                    fprintf(stderr, "Error: invalid command\n");
                    return;}
                strcpy(one_command->output_file, command);
                command = strtok_r(rest_command, " ", &rest_command);
                if(command != NULL){ 
                    fprintf(stderr, "Error: invalid command\n");
                    return;}    
            }
            one_command->command[i+1] = NULL;
            one_command->next = head;
            head=one_command;
            return;
        }
        one_command->command[i]  = (char*)malloc(1001*sizeof(char));
        strcpy(one_command->command[i], command);
        i=i+1;
        command = strtok_r(rest_command, " ", &rest_command);
    }
    one_command->command[i] = NULL;
    one_command->next = head;
    head=one_command;
}


void command_line_parser(char* command_line){  //deal with pipe
    char command_line_new[1001];
    if(command_line[0]==0x7c||command_line[strlen(command_line)-1]==0x7c){
        fprintf(stderr, "Error: invalid command\n");
        return;
    }
    memcpy(command_line_new, command_line, 1001);
    char* command_prev = NULL; 
    char* command; 
    char* command_next; 
    char* rest_command = command_line_new;
    int is_first_command, is_last_command;
    command = strtok_r(rest_command, "|", &rest_command);
    int pipefd[2];
    int input_fd, output_fd;
    while (command){
        command_next = strtok_r(rest_command,  "|", &rest_command);
        if(command_prev==NULL) is_first_command = 1; else is_first_command = 0;
        if(command_next==NULL) is_last_command = 1; else is_last_command = 0;
        if(command_prev){ //input from previous
            input_fd = pipefd[0];
        }
        else input_fd = -1;
        if(command_next){ //output to next
            pipe(pipefd);
            output_fd = pipefd[1];
        }
        else output_fd = -1;
        
        if(command[0]==0x20)command = command+1;
        command_parser(command, is_first_command, is_last_command, input_fd, output_fd);
        command_prev=command;
        command=command_next;
    }
    my_system(head);
}


void sig_int(int x){ // send signal to child process
    W_jobs *temp = w_job_head;
    while(temp){
        if(x==2)kill(temp->pid, 9);
        temp=temp->next;
    }
    printf("\n");
    return;
}


void sig_ign(int x){
    printf("\n");
    return;
}


int main(){
    signal(SIGINT, sig_int);
    signal(SIGTSTP, sig_int);
    signal(SIGQUIT, sig_ign);
    signal(SIGTERM, sig_ign);
    signal(SIGSTOP, sig_ign);

    head_jobs->num = 0;
    head_jobs->check = 1;
    head_jobs->next = NULL;

    char working_directory[1024] = {0};
    char* current_folder;
    char slash = 0x2f;
    char* command_line = (char*)malloc(sizeof(char)*1002);
    while(1){
        w_job_head = NULL;
        getcwd(working_directory, sizeof(working_directory));
        current_folder = strrchr(working_directory, slash);
        printf("[nyush %s]$ ", current_folder+1);
        fgets(command_line, 1001, stdin);
        if(command_line[0] == 0x0A)continue;
        command_line[strlen(command_line)-1] = 0;
        command_line_parser(command_line);

        if(tail_jobs->check == 0){
            tail_jobs->command_line = (char*)malloc(1001*sizeof(char));
            strcpy(tail_jobs->command_line, command_line);
            tail_jobs->check =1;
            printf("[%d] Stopped\t%s\n", tail_jobs->num, tail_jobs->command_line);
        }
    }
}