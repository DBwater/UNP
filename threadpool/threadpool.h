#ifndef THREADPOOL
#define THREADPOOL

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include "locker.h"
template<typename T>
class threadpool{
public:
    threadpool(int pthread_number,int max_requests);
    ~threadpool();
    bool append(T* request);
private:
    static void* worker(void* arg);//工作线程用的函数，他从工作队列中取出任务并且执行之
    void run();
private:
    int m_thread_number;//线程池中线程的数量
    int m_max_requests;//请求队列中允许的最大请求数
    pthread_t* m_threads;//描述线程的数组
    std::list<T*> m_workqueue;//请求队列
    locker m_queuelocker;//保护请求队列的队列锁
    sem m_queuestat;//是否有任务需要处理
    bool m_stop;//是否结束线程
};

template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests)
    :m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(NULL){
    if(thread_number <=0||max_requests<=0){
        throw std::exception();
    }
    m_threads = new pthread_t[thread_number];
    if(!m_threads){
        throw std::exception();
    }
    //创建线程，并将他们设置为脱离线程
    for(int i=0;i<thread_number;i++){
        printf("create %dth thread\n",i);
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request){
    m_queuelocker.lock();
    if(m_workqueue.size>m_max_requests){
        m_queuelocker.unlook();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlook();
    m_queuestat.post();
    return true;
}
template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlook();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_fron();
        m_queuelocker.unlook();
        if(!request){
            continue;
        }
        request->process();
    }
}




























#endif // THREADPOOL

