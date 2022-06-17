#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

//信号量
class sem
{
public:
    //设置信号量为0，第二个参数为0代表当前进程的局部信号量，第三个参数代表信号量的值
    sem()
    {
        if(sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    //设置值信号量为num
    sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    //析构时销毁信号量，释放占用的资源
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    //sem_wait使得信号量减1，成功时wait函数返回true
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    //sem_post使得信号量加1，成功时post函数返回true
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

//互斥锁
class locker
{
public:
    //NULL代表使用默认的普通锁
    locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    //销毁互斥锁
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    //加锁，成功时返回true
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    //解锁，成功时返回true
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    //阻塞队列将使用get函数获得互斥锁结构体
    pthread_mutex_t* get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

//条件变量
class cond
{
public:
    //参数为NULL代表默认属性
    cond()
    {
        if(pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }
    //销毁条件变量
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    //将调用的线程放入等待队列中，游双的书中加了互斥锁，timewait类似
    bool wait(pthread_mutex_t* m_mutex)
    {
        return pthread_cond_wait(&m_cond, m_mutex) == 0;
        // pthread_mutex_lock(&m_mutex);
        // ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        // pthread_mutex_unlock(&m_mutex);
        // return ret == 0;
    }
    //等待目标条件变量，设置等待截止时间
    bool timewait(pthread_mutex_t* m_mutex, struct timespec t)
    {
        return pthread_cond_timedwait(&m_cond, m_mutex, &t) == 0;
    }
    //唤醒等待目标条件的一个线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    //唤醒等待目标条件的所有线程
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

#endif