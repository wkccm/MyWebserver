#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

//阻塞队列
template <class T>
class BlockQueue
{
public:
    BlockQueue(int maxSize=1000)
    {
        if(maxSize <= 0)
        {
            exit(-1);
        }

        m_maxSize = maxSize;
        m_array = new T[maxSize];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }
    //记得delete数组
    ~BlockQueue()
    {
        m_mutex.lock();
        if(m_array != NULL)
        {
            delete [] m_array;
        }
        m_mutex.unlock();
    }
    //清除队列
    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    //若队列已满返回true
    bool full()
    {
        m_mutex.lock();
        if(m_size >= m_maxSize)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //若队列已空返回true
    bool empty()
    {
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // bool front(T &value)
    // {
    //     m_mutex.lock();
    //     if(m_size == 0)
    //     {
    //         m_mutex.unlock();
    //         return false;
    //     }
    //     m_front = (m_front + 1) % m_maxSize;
    //     value = m_array[m_front];
    //     m_mutex.unlock();
    //     return true;
    // }

    // bool back(T &value)
    // {
    //     m_mutex.lock();
    //     if(m_size == 0){
    //         m_mutex.unlock();
    //         return false;
    //     }
    //     value = m_array[m_back];
    //     m_mutex.unlock();
    //     return true;
    // }

    //获取队列大小
    int size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();

        return tmp;
    }
    //获取队列上限
    int maxSize()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_maxSize;
        m_mutex.unlock();

        return tmp;
    }
    //向队列中添加一个元素
    bool push(const T &item)
    {
        m_mutex.lock();
        if(m_size >= m_maxSize)
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_maxSize;
        m_array[m_back] = item;
        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    //从队列中取出一个元素，注意front指向队首的前一个位置
    bool pop(T &item)
    {
        m_mutex.lock();
        while(m_size <= 0)
        {
            if(!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_maxSize;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // bool pop(T &item, int timeout)
    // {
    //     struct timespec t = {0, 0};
    //     struct timeval now = {0, 0};
    //     gettimeofday(&now, NULL);

    //     m_mutex.lock();
    //     if(m_size <= 0)
    //     {
    //         t.tv_sec = now.tv_sec + timeout / 1000;
    //         t.tv_nsec = (timeout % 1000) * 1000;
    //         if (!m_cond.timewait(m_mutex.get(), t))
    //         {
    //             m_mutex.unlock();
    //             return false;
    //         }
    //     }

    //     if(m_size <= 0)
    //     {
    //         m_mutex.unlock();
    //         return false;
    //     }

    //     m_front = (m_front + 1) % m_maxSize;
    //     item = m_array[m_front];
    //     m_size--;
    //     m_mutex.unlock();
    //     return true;
    // }

private:
    locker m_mutex;
    cond m_cond;
    T *m_array;
    int m_size;
    int m_maxSize;
    int m_front;
    int m_back;
};

#endif