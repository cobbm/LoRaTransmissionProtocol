#pragma once
#include <Arduino.h>

#define DEBUG 1
/**
 * @brief Simple circular buffer implementation
 *
 * @tparam T base type of the circular buffer
 */

// TODO: add enqueue(T* arr, size_t len); and T* dequeue(size_t len);
//       to allow enqueue/dequeue mulptiple objects in one go
template <class T>
class CircularBuffer
{
public:
    CircularBuffer(int maxCapacity) : m_maxCapacity(maxCapacity), m_count(0)
    {
        this->m_buffer = new T[maxCapacity];
    };

    CircularBuffer(CircularBuffer<T> const &copy)
    {
        m_count = copy.m_count;
        m_head = copy.m_head;
        m_tail = copy.m_tail;

        m_maxCapacity = copy.m_maxCapacity;
        m_buffer = new T[copy.m_maxCapacity];

        std::copy(&copy.m_buffer[0], &copy.m_buffer[copy.m_maxCapacity], m_buffer);
    };

    ~CircularBuffer()
    {
#if DEBUG > 0
        Serial.println("CircularBuffer<T>: Destructor called");
#endif
        delete[] this->m_buffer;
    };

    CircularBuffer<T> &operator=(CircularBuffer<T> rhs)
    {
        rhs.swap(*this);
        return *this;
    };

    T *operator[](size_t i)
    {
        return &m_buffer[(m_head + i) % m_maxCapacity];
    };

    void swap(CircularBuffer<T> &s) noexcept
    {
        using std::swap;
        swap(this.m_buffer, s.m_buffer);
        swap(this.m_maxCapacity, s.m_maxCapacity);
        swap(this.m_head, s.m_head);
        swap(this.m_tail, s.m_tail);
        swap(this.m_count, s.m_count);
    };

    // C++11
    CircularBuffer<T>(CircularBuffer<T> &&src) noexcept
        : m_count(0),
          m_maxCapacity(0),
          m_buffer(NULL)
    {
        src.swap(*this);
    };

    CircularBuffer<T> &operator=(CircularBuffer<T> &&src) noexcept
    {
        src.swap(*this);
        return *this;
    };

    // ~CircularBuffer();

    // size_t size();
    // size_t count();

    // bool enqueue(const T &elem);
    // T *enqueueEmpty();

    // T *peek();
    // T *dequeue();

    size_t size()
    {
        return this->m_maxCapacity;
    };

    size_t count()
    {
        return this->m_count;
    };

    bool enqueue(const T &elem)
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
    };

    T *enqueueEmpty()
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
        T *elem = &this->m_buffer[this->m_tail++];
        this->m_tail %= this->m_maxCapacity;
        this->m_count++;
        // zero out the data
        memset(elem, 0, sizeof(T));
        return elem;
    };

    T *peek()
    {
        if (this->m_count <= 0)
        {
            return nullptr;
        }
        return &this->m_buffer[this->m_head];
    };

    T *dequeue()
    {
        if (this->m_count <= 0)
        {
            return nullptr;
        }
        T *elem = &this->m_buffer[this->m_head++];
        this->m_head %= this->m_maxCapacity;
        this->m_count--;
        return elem;
    };

private:
    T *m_buffer = nullptr;
    size_t m_maxCapacity;
    size_t m_count;

    int m_head = 0;
    int m_tail = 0;
};