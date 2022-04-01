#pragma once
#include <Arduino.h>

template <class T>
class CircularBuffer
{
public:
    CircularBuffer(int maxCapacity);

    CircularBuffer(CircularBuffer<T> const &copy)
    {
        m_count = copy.m_count;
        m_head = copy.m_head;
        m_tail = copy.m_tail;

        m_maxCapacity = copy.m_maxCapacity;
        m_buffer = new T[copy.m_maxCapacity];

        std::copy(&copy.m_buffer[0], &copy.m_buffer[copy.m_maxCapacity], m_buffer);
    }

    CircularBuffer<T> &operator=(CircularBuffer<T> rhs)
    {
        rhs.swap(*this);
        return *this;
    }

    void swap(CircularBuffer<T> &s) noexcept
    {
        using std::swap;
        swap(this.m_buffer, s.m_buffer);
        swap(this.m_maxCapacity, s.m_maxCapacity);
        swap(this.m_head, s.m_head);
        swap(this.m_tail, s.m_tail);
        swap(this.m_count, s.m_count);
    }

    // C++11
    CircularBuffer<T>(CircularBuffer<T> &&src) noexcept
        : m_count(0), m_maxCapacity(0), m_buffer(NULL)
    {
        src.swap(*this);
    }

    CircularBuffer<T> &operator=(CircularBuffer<T> &&src) noexcept
    {
        src.swap(*this);
        return *this;
    }

    ~CircularBuffer();

    size_t size();
    size_t count();

    bool enqueue(const T &elem);
    T *enqueueEmpty();

    T *peek();
    T *dequeue();

private:
    T *m_buffer;
    size_t m_maxCapacity;
    size_t m_count;

    int m_head = 0;
    int m_tail = 0;
};