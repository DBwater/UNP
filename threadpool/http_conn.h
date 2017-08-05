#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<string.h>
#include<pthread.h>
#include<string.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/mman.h>
#include<errno.h>
#include<stdarg.h>
#include<sys/uio.h>
#include "locker.h"
class http_conn
{
public:
    static const int FILENAME_LEN = 200;//文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;//读缓冲区的大小
    static const int WRITE_BUFFER_SZIE = 1024;//写缓冲区的大小
    enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNERCT,PATCH};//http请求的方法，目前仅支持GET
    //解析客户请求时候，主状态机的状态，分别表示：当前正在分析请求行，当前正在分析头部字段,正在分析消息体
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};//
    //服务器处理http请求的可能结果，NOREQUST:请求不完整，需要继续读取数据;GET_REQUEST:获得一个完整的请求;BAD_REQUEST:客户请求有语法错误;
    //FORBIDDEN_REQUEST:客户对资源没有足够的访问权限;INTERNAL_ERROR:服务器内部错误;CLOSED_CONNECTION:客户端已经关闭连接
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
    //行的读取状态,读取到一个完整的行，行出错，行数据尚不完整
    enum LINE_STATUS{LINE_OK=0,LINE_BAD,LINE_OPEN};
public:
    http_conn();
    ~http_conn();
public:
    void init(int sockfd,const sockaddr_in& addr);//初始化新接受的连接
    void close_conn(bool real_close = true);//关闭连接
    void process();//处理客户的请求
    bool read();//非阻塞的读
    bool write();
private:
    void init();//初始化连接
    HTTP_CODE process_read();//解析http请求
    bool process_write(HTTP_CODE ret);//填充http应答

    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    //char* getline{return m_read_buf+m_start_line;}
    LINE_STATUS parse_line();
    //被process_write调用以填充http应答
    void unmap();
    bool add_response(const char* format,...);
    bool add_headers(int content_len);
    bool add_content(const char*content);
    bool add_status_line(int status,const char* title);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
public:
    //所有的socket上发生的事情都被注册到同一个epoll内核事件中，所以将epoll文件描述符设置为静态的
    static int m_epollfd;
    //统计用户数量
    static int m_user_count;
private:
    //该http的socket地址和对方的socket地址
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    int m_read_idx; //标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;//当前正在分析的字符的在读缓冲区中的位置
    int m_start_line;//当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SZIE];//写缓冲区
    int m_write_idx;//写缓冲区中待发送的数据
    CHECK_STATE m_check_state;//主状态机当前所处的位置
    METHOD m_method;//请求方法
    char m_real_file[FILENAME_LEN];//客户请求的目标的文件的完整路径
    char* m_url;//客户请求的文件名
    char* m_host;//主机名
    char* m_version;//http协议版本号码，目前仅http1.1
    int m_content_length;//请求的消息体的长度
    bool m_linger;//http是否保持连接
    char *m_file_address;//客户请求的目标文件被mmap到内存的起始位置
    struct stat m_file_stat;//目标文件名的状态，通过它我们可以判断文件是否存在，是否为目录，是否可读，并获取文件的大小等信息
    //我们用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量
    struct iovec m_iv[2];
    int m_iv_count;
};

#endif // HTTP_CONN_H
