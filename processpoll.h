#ifndef PROCESSPOLL_H
#define PROCESSPOLL_H

#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<assert.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<stdlib.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include<sys/wait.h>
#include<sys/stat.h>
//描述一个子进程,m_pid是目标的子进程的ＰＩＤ，m_pipefd是父子进程通信用的管道
class process{
public:
        process():m_pid(-1){}
public:
        pid_t m_pid;
        int m_pipefd[2];
};
//进程池类
template<typename T>
class processpool{
private:
    processpool(int listenfd,int process_number = 8);
public:
    static processpool<T>* create(int listenfd,int process_number = 8){
        if(!m_instance){
            m_instance = new processpool<T>(listenfd,process_number);
        }
        return m_instance;
    }
    ~processpool(){
        delete [] m_sub_process;

    }
    void run();
private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();
private:
    static const int MAX_PRECESS_NUMBER = 16;//进程池的最大子进程的数量
    static const int USER_PER_PROCESS = 65536;//每个子进程最多能处理的客户数量
    static const int MAX_EVENT_NUMBER = 10000;//epoll最多的处理的事件数量
    int m_process_number; //进程池中的进程总数
    int m_idx;//子进程在池中的序号，从０开始
    int m_epollfd; //每一进程都有一个epoll的内核事件表
    int m_listenfd;//监听socket
    int m_stop;//子进程通过m_stop来决定是否停止运行
    process* m_sub_process;//保存所有子进程的描述信息
    static processpool<T>* m_instance;//进程池的静态实例
};

template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;
static int sig_pipefd[2];//用于信号处理的管道。实现统一事件源

static int setnonblocking(int fd){
    int old_flags = fcntl(fd,F_GETFL);
    int new_flags = old_flags|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flags);
    return old_flags;
}

static void addfd(int epollfd,int fd){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN|EPOLLET;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

template<typename T>
processpool<T>::processpool(int listenfd, int process_number)
    :m_listenfd(listenfd),m_process_number(process_number),m_idx(-1),m_stop(false)
{
    assert(process_number>0&&process_number<=MAX_PRECESS_NUMBER);
    m_sub_process = new process[process_number];
    assert(m_sub_process);
    for(int i=0 ; i < process_number ; i++){
        int ret =socketpair(AF_UNIX,SOCK_STREAM,0,m_sub_process[i].m_pipefd);
        assert(ret == 0);
        m_sub_process[i].m_pid = fork();
        if(m_sub_process[i].m_pid>0){
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        }
        else{
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break;
        }
    }
}
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1],(char*)&msg,1,0);
    errno = save_errno;
}
//设置信号处理函数
void addsig(int sig,void(handler)(int),bool restart = true)
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    if(restart) sa.sa_flags|= SA_RESTART;
    sigfillset(&sa.sa_mask);
    int ret = sigaction(sig,&sa,NULL);
    assert(ret!=-1);
}

/*统一信号源，将信号和I/O事件统一起来交给主程序处理一般方法是：信号发送函数将信号值写到管道一端，然后主程序采用IO复用技术监听管道读端，
一旦事件发生那么主程序可以在事件处理逻辑中定义信号处理方法，这样就和IO事件处理一只。*/
template<typename T>
void processpool<T>::setup_sig_pipe()
{
    m_epollfd = epoll_create(5);
    assert(m_epollfd!=-1);
    int ret = socketpair(PF_UNIX,SOCK_STREAM,0,sig_pipefd);
    assert(ret!=-1);
    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd,sig_pipefd[0]);
    //设置信号处理函数
    addsig(SIGCHLD,sig_handler);
    addsig(SIGTERM,sig_handler);
    addsig(SIGINT,sig_handler);
    addsig(SIGPIPE,SIG_IGN);
}

template<typename T>
void processpool<T>::run()
{
    if(m_idx == -1){
        run_child();
        return;
    }
    run_parent();
}
template<typename T>
void processpool<T>::run_child()
{
    //setup_sig_pipe();
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    addfd(m_epollfd,pipefd);
    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T [USER_PER_PROCESS];
    assert(users);
    int number = 0;
    int ret = -1;
    while(!m_stop){
        number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if(number<0&&errno!=EINTR){
            printf("epoll failure\n");
            break;
        }
        for(int i = 0;i < number ; i++){
            int sockfd = events[i].data.fd;
            //客户连接请求
            if((sockfd == pipefd) && (events[i].events&EPOLLIN)){
                int client = 0;
                //从父子进程管道读取数据，如果读取成功说明有客户连接进来
                ret = recv(sockfd,(char*)&client,sizeof(client),0);
                if((ret<0)&&(errno!=EAGAIN)||ret == 0){
                    continue;
                }
                else{
                    struct sockaddr_in client_addr;
                    socklen_t client_addrlen = sizeof(client_addr);
                    int connfd = accept(sockfd,(struct sockaddr*)&client_addr,&client_addrlen);
                    if(connfd<0){
                        printf("errno is: %d\n",errno);
                        continue;
                    }
                    addfd(m_epollfd,connfd);
                    users[connfd].init(m_epollfd,connfd,client_addr);
                }
            }
            //子进程收到的信号
            else if((sockfd == sig_pipefd[0])&&(events[i].events&EPOLLIN)){
                int sig;
                char signalss[1024];
                ret = recv(sig_pipefd[0],signalss,sizeof(signalss),0);
                //ret = recv(sig_pipefd[0],signals,sizeof(signals),0);
                if(ret<=0)continue;
                else{
                    for(int i=0;i<ret;i++){
                        switch(signalss[i]){
                            case SIGCHLD://子进程状态发生变化（停止）
                                pid_t pid;
                                int stat;
                                while((pid=waitpid(-1,&stat,WNOHANG))>0)continue;//防止僵尸进程的出现
                                break;
                            case SIGTERM://终止进程
                            case SIGINT://键盘输入中断进程
                                m_stop = true;
                                break;
                            default:
                                break;

                        }
                    }
                }
            }
            //客户的请求和发送的信息
            else if(events[i].events&EPOLLIN){
                users[sockfd].process();
            }
            else{
                continue;
            }
        }
    }
    delete [] users;
    users = NULL;
    close(pipefd);
}
template<typename T>
void processpool<T>::run_parent()
{
   //setup_sig_pipe();
   addfd(m_epollfd,m_listenfd);
   epoll_event events[MAX_EVENT_NUMBER];
   int sub_peocess_count =0;
   int new_conn = 1;
   int number =0;
   int ret = -1;
   while(!m_stop){
       number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
       if((number,0)&&(errno!=EINTR)){
           printf("epoll failure\n");
           break;
       }
       for(int i=0;i<number;i++){
           int sockfd = events[i].data.fd;
           if(sockfd == m_listenfd){
               //如果有连接到来，就采用Round Robin的方式将其分配给一个子进程处理
               int j=sub_peocess_count;
               do{
                   if(m_sub_process[j].m_pid!=-1){
                       break;
                   }
                   j = (j+1)%m_process_number;
               }while(j!=sub_peocess_count);
               if(m_sub_process[j].m_pid == -1){
                   m_stop = true;
                   break;
               }
               sub_peocess_count =(j+1)%m_process_number;
               send(m_sub_process[j].m_pipefd[0],(char*)&new_conn,sizeof(new_conn),0);
               printf("send request to child %d\n",i);

           }
           //父进程收到信号
           else if((sockfd ==sig_pipefd[0])&&events[i].events&EPOLLIN){
               int sig;
               char signalss[1024];
               ret = recv (sig_pipefd[0],signalss,sizeof(signalss),0);
               if(ret<=0)return 0;
               else{
                  for(int i=0;i<ret;i++){
                      switch(signalss[i]){
                       case SIGCHLD:
                          pid_t pid;
                          int stat;
                          while((pid = waitpid(-1,&stat,WNOHANG))>0){
                               for(int i=0;i<m_process_number;i++){
                                   /*如果进程池中的第ｉ个进程退出了，那么主进程应该关闭相应的通信通道，并设置相应的m_pid
                                   为-1,标记该子进程已经退出了*/

                                   if(m_sub_process[i].m_pid == pid){
                                        printf("child %d join\n",i);
                                        close(m_sub_process[i].m_pipefd[0]);
                                        m_sub_process[i].m_pid = -1;
                                    }
                               }
                           }
                          m_stop = true;
                          for(int i=0;i<m_process_number;i++){
                              if(m_sub_process[i].m_pid!=-1){
                                  m_stop=false;
                              }
                          }
                        break;
                        case SIGTERM:
                        case SIGINT:
                        printf("kill all the child now\n");
                        for(int i=0;i<m_process_number;i++){
                            int pid = m_sub_process[i].m_pid;
                            if(pid!=-1){
                                kill(pid,SIGTERM);
                            }
                        }
                        break;
                      default:
                          break;
                      }
                   }
               }
           }
           else{continue;}
       }
   }
   //close(m_listenfd);
   close(m_epollfd);
}










#endif // PROCESSPOLL_H

