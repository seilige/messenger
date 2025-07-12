#pragma once
#include "net_common.h"

namespace olc
{
    namespace net
    {
        template<typename T>
        class tsQueue
        {
        public:
            tsQueue() = default;
            tsQueue(const tsQueue<T>&) = delete;

            // Returns a reference to the front element of the queue
            const T& front()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                return deqQueue.front();
            }

            // Adds an element to the back of the queue (copy version)
            void push_back(const T& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.push_back(item);

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Adds an element to the back of the queue (move version)
            void push_back(T&& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.emplace_back(std::move(item));

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Adds an element to the front of the queue (copy version)
            void push_front(const T& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.push_front(item);

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Adds an element to the front of the queue (move version)
            void push_front(T&& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.emplace_front(std::move(item));

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Removes the front element from the queue
            void pop_front()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                if (!deqQueue.empty())
                    deqQueue.pop_front();
            }

            // Checks if the queue is empty
            bool empty()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                return deqQueue.empty();
            }

            // Returns the number of elements in the queue
            size_t size()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                return deqQueue.size();
            }

            // Clears all elements from the queue
            void clear()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.clear();
            }

            // Blocks the calling thread until the queue has at least one element
            void wait()
            {
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.wait(ul, [this] {
                    // Check if there are any elements in the queue
                    std::lock_guard<std::mutex> lock(muxQueue);
                    return !deqQueue.empty();
                    });
            }

        protected:
            // Mutex for thread-safe access to the queue
            std::mutex muxQueue;
            std::deque<T> deqQueue;

            // Condition variable and mutex for blocking operations
            std::condition_variable cvBlocking;
            std::mutex muxBlocking;
        };
    }
}