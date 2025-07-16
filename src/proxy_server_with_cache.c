#include "proxy_parse.h"
#include <stdio.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>
#include<pthread.h>
#include<semaphore.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>


#define MAX_CLIENTS 10
#define MAX_BYTES 10*(1<<10)

typedef struct cache_element cache_element;

struct cache_element{
    char *data;
    int len;
    char *url;
    time_t lru_time_track;
    cache_element *next;
};

cache_element *find(char *url);
int add_cache_element(char *data,int size,char *url);
void remove_cache_element();

int port_number=8080;

int proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

cache_element * head;
int cache_size ;

int handle_request(int clientSocketId,struct ParsedRequest *request,char *tempReq){
    char *buf =(char *)malloc(sizeof(char)*MAX_BYTES);
    strcpy(buf,"GET");
    strcat(buf,request->path);
    strcat(buf," ");
    strcat(buf,request->version);
    strcat(buf,"\r\n");

    size_t len=strlen(buf);
    
}

void *thread_fn(void *socketNew){
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore,&p);
    printf("Semaphore value is : %d\n",p);
    int *t=(int *)socketNew;
    int socket=*t;
    int bytes_sent,len;

    char *buffer =(char *)calloc(MAX_BYTES,sizeof(char));
    bzero(buffer,MAX_BYTES);

    bytes_sent = recv(socket,buffer,MAX_BYTES,0);
    if(bytes_sent<0){
        perror("Failed to receive data\n");
    }

    while(bytes_sent>0){
        len=strlen(buffer);
        if(strstr(buffer,"\r\n\r\n")==NULL){
            bytes_sent = recv(socket,buffer+len,MAX_BYTES-len,0);
        }else break;
    }

    char *tempReq =(char *)malloc(strlen(buffer)*sizeof(char)+1);
    for(int i=0;i<strlen(buffer);i++){
        tempReq[i]=buffer[i];
    }

    struct cache_element *temp=find(tempReq);
    if(temp!=NULL){
        int size=temp->len/sizeof(char);
        int pos=0;
        char response[MAX_BYTES];
        while(pos<size){
            bzero(response,MAX_BYTES);
            for(int i=0;i<MAX_BYTES;i++){
                response[i]=temp->data[i];
                pos++;
            }

            send(socket,response,MAX_BYTES,0);
        }

        printf("Data received from the cache\n");
        printf("%s\n\n",response);
    }else if(bytes_sent>0){
        len=strlen(buffer);
        struct ParsedRequest *request = ParsedRequest_create();

        if(ParsedRequest_parse(request,buffer,len)<0){
            printf("Parsing Failed\n");
        }else{
            bzero(buffer,MAX_BYTES);
            if(!strcmp(request->method,"GET")){
                if(request->host && request->path && checkHTTPversion(request->version)==1){
                    bytes_sent = handle_request(socket,request,tempReq);
                    if(bytes_sent<0){
                        sendErrorMessage(socket,500);
                    }
                }else{
                    sendErrorMessage(socket,500);
                }
            }else{
                printf("This code doesnt support any method apart from GET \n");
            }
        }

        ParsedRequest_destroy(request);
    }else if(bytes_sent==0){
        printf("Client disconnected\n");

    }
    shutdown(socket,SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore,&p);
    printf("Semaphore post value is %d\n",p);
    free(tempReq);
    return NULL;
}

int main(int argc,char * argv[]){
    socklen_t client_len;
    int client_socketId;
    struct sockaddr_in server_addr,client_addr;
    sem_init(&semaphore,0,MAX_CLIENTS);
    pthread_mutex_init(&lock,NULL);

    if(argc==2){
        port_number=atoi(argv[1]);
    }else{
        printf("To few arguments..\n");
        exit(1);
    }

    printf("Starting proxy server at port %d\n : ",port_number);
    proxy_socketId=socket(AF_INET,SOCK_STREAM,0);

    if(proxy_socketId < 0){
        perror("Failed to create socket\n");
        exit(1);
    }

    int reuse=1;
    if(setsocketopt(proxy_socketId,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse))<0){
        perror("SetSockOpt Failed\n");
        exit(1);
    }

    bzero((char*)&server_addr,sizeof(server_addr));
    
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port_number);
    server_addr.sin_addr.s_addr=INADDR_ANY;
    if(bind(proxy_socketId,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("Port is not available\n");
        exit(1);
    }

    printf("Binding to port %d\n",port_number);

    int listen_status=listen(proxy_socketId,MAX_CLIENTS);
    if(listen_status<0){
        perror("Listen Failed\n");
        exit(1);
    }

    int i=0;
    int Connected_socketId[MAX_CLIENTS];
    while(1){
        bzero((char*)&client_addr,sizeof(client_addr));
        client_len=sizeof(client_addr);

        client_socketId=accept(proxy_socketId,(struct sock_addr*)&client_addr,(socklen_t*)&client_len);
        if(client_socketId<0){
            printf("Not able to connect\n");
            exit(1);
        }else{
            Connected_socketId[i]=client_socketId;
        }

        struct sockaddr_in *client_pt=(struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr=client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&ip_addr,str,INET_ADDRSTRLEN);

        printf("Client is connected on port : %d  and ip address : %s \n",ntohs(client_addr.sin_port),str);

        pthread_create(&tid[i],NULL,thread_fn,(void *)&Connected_socketId[i]);
        i++;
    }
    close(proxy_socketId);

    return 0;
}