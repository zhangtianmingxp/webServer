#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>


// 线程同步机制封装类
class sem{

public:
    //对指定信号量进行初始化
    sem(){
        if(sem_init(&m_sem, 0, 0)!=0){
            throw  std::exception();
        }
    }
    sem(int num){
        if(sem_init(&m_sem, 0, num)!=0){
            throw  std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }

    // 等待信号量
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }

    //增加信号量
    bool post(){
        return sem_post(&m_sem)==0;
    }


private:
    sem_t m_sem;
};




// 互斥锁
class locker{

public:
    locker(){
        if(pthread_mutex_init(&m_mutex, NULL)!=0){
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }
    pthread_mutex_t * get(){
        return &m_mutex;
    }


private:
    pthread_mutex_t m_mutex;

};

// 条件变量类， 线程是否有数据
class cond{

public:
    cond(){
        if(pthread_cond_init(&m_cond, NULL)!=0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    
    bool wait(pthread_mutex_t * mutex){
        //函数将解锁mutex参数指向的互斥锁，并使当前线程阻塞在 m_cond 指向的条件变量上。
        return pthread_cond_wait(&m_cond, mutex)==0;
    }

    bool timedwait(pthread_mutex_t * mutex, struct timespec t){
        //函数到了一定的时间，即使条件未发生也会解除阻塞。这个时间由参数 t 指定。函数返回时，相应的互斥锁往往是锁定的，即使是函数出错返回。
        return pthread_cond_timedwait(&m_cond, mutex, &t)==0;
    }
    
    bool signal(){
        //释放被阻塞在指定条件变量上的一个线程
        return pthread_cond_signal(&m_cond)==0;
    }
    
    bool broadcast(){
        //让所有的条件变量都唤醒
        return pthread_cond_broadcast(&m_cond)==0;
    }
private:
    pthread_cond_t m_cond;
};



#endif