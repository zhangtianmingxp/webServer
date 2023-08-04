#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <exception>
#include <stdio.h>


//线程池类， 定义成模板类是为了代码的复用，模板参数T是任务类
template<typename T>
class threadpool{
public:
    threadpool(int thread_number=8, int max_requests=10000);
    ~threadpool();
    bool append(T* requst);
    

private:
    static void* worker(void* arg);
    void run();


private:
    
    int m_thread_number;        //线程数量
    pthread_t * m_threads;      //线程池数组，大小为 m_thread_number
    int m_max_requests;         //请求队列中最多允许的，等待处理的请求数量
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //互斥锁
    sem m_queuestat;            //信号量用来判断是否有任务需要处理
    bool m_stop;                //是否结束线程
};


template<typename T>   //线程池构造函数
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL){
        if((thread_number<=0)||(max_requests<=0)){
            throw std::exception(); 
        }
        //创建线程数组
        m_threads = new pthread_t[m_thread_number];
        if(!m_threads){
            throw std::exception();
        }
        
        //创建thread_number个线程，并将它们设置为线程脱离
        for(int i=0; i<thread_number; i++){
            printf("create the %dth thread\n",i);
            //参数：线程数组中的线程，线程的属性，以函数指针的方式指明新建线程需要执行的函数，指定传递给 start_routine 函数的实参
            if(pthread_create(m_threads + i, NULL, worker, this)!=0){
                //失败咱们要释放数组
                delete [] m_threads;
                throw std::exception();
            }

            //主线程与子线程分离，子线程结束后，资源自动回收
            if(pthread_detach(m_threads[i])!=0){
                delete [] m_threads;
                throw std::exception();
            }
        }
    }

template<typename T>   //析构函数
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

template<typename T> 
bool threadpool<T>::append(T *request){

    m_queuelocker.lock();
    if(m_workqueue.size()> m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T> 
void* threadpool<T>::worker(void * arg){
    threadpool * pool = (threadpool *) arg;
    pool->run();
    return pool;
}

template<typename T> 
void threadpool<T>::run(){
    //运行
    while(!m_stop){
        //信号量有值则堵塞，不堵塞值-1；
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();  //弹出请求队列
        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        // 调用任务类的process(), 处理客户端的请求
        request->process();
    }
}






#endif