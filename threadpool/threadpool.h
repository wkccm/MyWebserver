#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class ThreadPool
{
public:
    ThreadPool(int actorModel, ConnectionPool *connPool, int threadNumber = 8, int maxRequest = 10000);
    ~ThreadPool();
    bool append(T *request, int state);

private:
    static void *worker(void *arg);
    void run();

private:
    int m_threadNumber;
    int m_maxRequest;
    pthread_t *m_threads;
    std::list<T *> m_workQueue;
    locker m_queueLocker;
    sem m_queueStat;
    ConnectionPool *m_connPool;
    int m_actorModel;
};

template <typename T>
ThreadPool<T>::ThreadPool(int actorModel, ConnectionPool *connPool, int threadNumber, int maxRequest):m_actorModel(actorModel), m_threadNumber(threadNumber),m_maxRequest(maxRequest), m_threads(NULL), m_connPool(connPool)
{
    if(threadNumber <= 0 || maxRequest <= 0)
    {
        throw std::exception();
    }
    m_threads = new pthread_t[m_threadNumber];
    if(!m_threads)
    {
        throw std::exception();
    }

    for (int i = 0; i < threadNumber; ++i)
    {
        if(pthread_create(m_threads +i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
}

template <typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    m_queueLocker.lock();

    if(m_workQueue.size() >= m_maxRequest)
    {
        m_queueLocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workQueue.push_back(request);
    m_queueLocker.unlock();
    m_queueStat.post();
    return true;
}

template <typename T>
void *ThreadPool<T>::worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void ThreadPool<T>::run()
{
    while(true)
    {
        m_queueStat.wait();
        m_queueLocker.lock();

        if(m_workQueue.empty())
        {
            m_queueLocker.unlock();
            continue;
        }
        T *request = m_workQueue.front();
        m_workQueue.pop_front();
        m_queueLocker.unlock();
        if(!request)
        {
            continue;
        }
        if(m_actorModel == 1)
        {
            if(request->m_state == 0)
            {
                // 读成功，则连接数据库
                if(request->readOnce())
                {
                    request->improv = 1;
                    ConnectionRAII mysqlConn(&request->mysql, m_connPool);
                    request->process();
                }
                // 读失败，则将定时器标志位置1
                else
                {
                    request->improv = 1;
                    request->timerFlag = 1;
                }
            }
            // 写成功，则不需要连接数据库
            else
            {
                if(request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timerFlag = 1;
                }
            }
        }
        else
        {
            ConnectionRAII mysqlConn(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif