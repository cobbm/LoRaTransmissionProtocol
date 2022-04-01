#include "CircularBuffer.hpp"

template <class T>
CircularBuffer<T>::CircularBuffer(int maxCapacity) : m_maxCapacity(maxCapacity)
{
    this->m_buffer = new T[maxCapacity];
}

template <class T>
CircularBuffer<T>::~CircularBuffer()
{
    delete[] this->m_buffer;
}

template <class T>
size_t CircularBuffer<T>::size()
{
    return this->m_maxCapacity;
}

template <class T>
size_t CircularBuffer<T>::count()
{
    return this->m_count;
}

template <class T>
bool CircularBuffer<T>::enqueue(const T &elem)
{
    if (this->m_count >= this->m_maxCapacity)
    {
        return false;
    }
    if (this->m_count == 0)
    {
        this->m_head = 0;
        this->m_tail = 0;
    }
    this->m_buffer[this->m_tail++] = elem;
    this->m_tail %= this->m_maxCapacity;
    this->m_count++;
    return true;
}

template <class T>
T *CircularBuffer<T>::enqueueEmpty()
{
    if (this->m_count >= this->m_maxCapacity)
    {
        return nullptr;
    }
    if (this->m_count == 0)
    {
        this->m_head = 0;
        this->m_tail = 0;
    }
    T *elem = this->m_buffer[this->m_tail++];
    this->m_tail %= this->m_maxCapacity;
    this->m_count++;
    // zero out the data
    memset(elem, 0, sizeof(T));
    return elem;
}

template <class T>
T *CircularBuffer<T>::peek()
{
    if (this->m_count <= 0)
    {
        return nullptr;
    }
    return this->m_buffer[this->m_head];
}

template <class T>
T *CircularBuffer<T>::dequeue()
{
    if (this->m_count <= 0)
    {
        return nullptr;
    }
    T *elem = this->m_buffer[this->m_head++];
    this->m_head %= this->m_maxCapacity;
    this->m_count--;
    return elem;
}
