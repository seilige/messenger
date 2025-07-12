#pragma once
#include "net_common.h"
#include "net_tsQueue.h"
#include "net_message.h"
#include "net_connection.h"

namespace olc
{
    namespace net
    {
        template<typename T>
        class server_interface
        {
        public:
            // Constructor: Initialize server on specified port
            server_interface(uint16_t port)
                : m_asioAcceptor(m_asioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
            {
            }

            // Destructor: Ensure server is properly stopped
            virtual ~server_interface()
            {
                stop();
            }

            // Called when a client has been validated and is ready for communication
            virtual void onClientValidated(std::shared_ptr<connection<T>> client)
            {
                // By default, simply log that the client has been validated
                // This can be overridden in derived classes for custom validation logic
                std::cout << "[SERVER] Client " << client->getID() << " validated" << std::endl;

                // Here you could add custom logic for handling newly validated clients
                // For example, sending welcome messages or notifying other clients
            }

            // Start the server: Begin accepting connections and processing messages
            bool start()
            {
                try
                {
                    // Begin waiting for client connections
                    waitForClientConnection();

                    // Start the ASIO context in a separate thread
                    m_threadContext = std::thread([this]() { m_asioContext.run(); });
                }
                catch (std::exception& e)
                {
                    std::cerr << "[SERVER] Exception: " << e.what() << "\n";
                    return false;
                }

                std::cout << "[SERVER] Started!\n";
                return true;
            }

            // Stop the server: Halt ASIO context and join threads
            void stop()
            {
                // Stop the ASIO context
                m_asioContext.stop();

                // Wait for the context thread to finish
                if (m_threadContext.joinable())
                    m_threadContext.join();

                std::cout << "[SERVER] Stopped!\n";
            }

            // ASYNC - Wait for and handle incoming client connections
            void waitForClientConnection()
            {
                m_asioAcceptor.async_accept(
                    [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
                    {
                        if (!ec)
                        {
                            std::cout << "[SERVER] New Connection: " << socket.remote_endpoint() << "\n";

                            // Create a new connection object for this client
                            std::shared_ptr<connection<T>> newconn =
                                std::make_shared<connection<T>>(connection<T>::owner::server,
                                    m_asioContext,
                                    std::move(socket),
                                    m_qMessagesIn);

                            // Check if this client connection should be accepted
                            if (onClientConnect(newconn))
                            {
                                m_deqConnections.push_back(std::move(newconn));

                                // Begin receiving messages from this client
                                m_deqConnections.back()->connectToClient(this, nIDCounter++);

                                std::cout << "[" << m_deqConnections.back()->getID() << "] Connection Approved\n";
                            }
                            else
                            {
                                std::cout << "[-----] Connection Denied\n";
                            }
                        }
                        else
                        {
                            std::cerr << "[SERVER] New Connection Error: " << ec.message() << "\n";
                        }

                        // Continue waiting for more connections recursively
                        waitForClientConnection();
                    });
            }
            
            // Send a message to a specific client
            void messageClient(std::shared_ptr<connection<T>> client, const message<T>& msg)
            {
                if (client && client->isConnected())
                {
                    client->send(msg);
                }
                else
                {
                    onClientDisconnect(client);
                    client.reset();
                    m_deqConnections.erase(
                        std::remove(m_deqConnections.begin(), m_deqConnections.end(), client), m_deqConnections.end());
                }
            }

            // Send a message to all connected clients, optionally excluding one
            void messageAllClients(const message<T>& msg, std::shared_ptr<connection<T>> pIgnoreClient = nullptr)
            {
                bool invalidClientExists = false;

                for (auto& client : m_deqConnections)
                {
                    if (client && client->isConnected())
                    {
                        if (client != pIgnoreClient)
                            client->send(msg);
                    }
                    else
                    {
                        onClientDisconnect(client);
                        client.reset();
                        invalidClientExists = true;
                    }
                }

                if (invalidClientExists)
                {
                    m_deqConnections.erase(
                        std::remove(m_deqConnections.begin(), m_deqConnections.end(), nullptr), m_deqConnections.end());
                }
            }

            // Process incoming messages from the message queue
            void update(size_t maxMessages = -1, bool wait = false)
            {
                if (wait && m_qMessagesIn.empty())
                {
                    // Wait until a message arrives
                    m_qMessagesIn.wait();
                }

                size_t messageCount = 0;
                while (messageCount < maxMessages && !m_qMessagesIn.empty())
                {
                    // Get the next message from the queue
                    auto msg = m_qMessagesIn.front();
                    m_qMessagesIn.pop_front();

                    // Process the message
                    onMessage(msg.remote, msg.msg);

                    messageCount++;
                }
            }

        protected:
            // These methods should be overridden in derived classes
            
            // Called when a client attempts to connect - return true to accept, false to deny
            virtual bool onClientConnect(std::shared_ptr<connection<T>> client)
            {
                return false;
            }

            // Called when a client disconnects
            virtual void onClientDisconnect(std::shared_ptr<connection<T>> client)
            {
            }

            // Called when a message is received from a client
            virtual void onMessage(std::shared_ptr<connection<T>> client, message<T>& msg)
            {
            }

        protected:
            // Thread-safe queue for incoming messages
            tsQueue<owned_message<T>> m_qMessagesIn;

            // ASIO context for handling I/O operations
            boost::asio::io_context m_asioContext;
            std::thread m_threadContext;

            // TCP acceptor for listening for new connections
            boost::asio::ip::tcp::acceptor m_asioAcceptor;

            // Container for all active client connections
            std::deque<std::shared_ptr<connection<T>>> m_deqConnections;

            // Counter for assigning unique IDs to clients
            uint32_t nIDCounter = 10000;
        };
    }
}