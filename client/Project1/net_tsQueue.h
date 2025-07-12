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

            // Returns the first element of the queue
            const T& front()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                return deqQueue.front();
            }

            // Adds an element to the back of the queue
            void push_back(const T& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.push_back(item);

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Adds an element to the back of the queue (move semantics)
            void push_back(T&& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.emplace_back(std::move(item));

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Adds an element to the front of the queue
            void push_front(const T& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.push_front(item);

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Adds an element to the front of the queue (move semantics)
            void push_front(T&& item)
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.emplace_front(std::move(item));

                // Notify waiting threads that new data is available
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            // Removes the first element from the queue
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

            // Returns the size of the queue
            size_t size()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                return deqQueue.size();
            }

            // Clears the queue
            void clear()
            {
                std::lock_guard<std::mutex> lock(muxQueue);
                deqQueue.clear();
            }

            // Blocks until at least one element is available in the queue
            void wait()
            {
                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.wait(ul, [this] {
                    // Check if there are elements in the queue
                    std::lock_guard<std::mutex> lock(muxQueue);
                    return !deqQueue.empty();
                    });
            }

        protected:
            // Mutex for thread-safe access to the queue
            std::mutex muxQueue;
            std::deque<T> deqQueue;

            // Mutex and condition variable for blocking operations
            std::condition_variable cvBlocking;
            std::mutex muxBlocking;
        };
    }
}