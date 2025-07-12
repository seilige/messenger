#pragma once
#include "net_common.h"
#include "net_tsQueue.h"
#include "net_message.h"
#include "net_connection.h"

// Enumeration defining custom message types for network communication
enum class CustomMsgTypes : uint32_t
{
    ServerAccept,
    ServerDeny,
    ServerPing,
    MessageAll,
    ServerMessage,
    KeyPress,      // Message type for key presses
    DirectMessage, // Message type for direct messages
    RequestClientList, // New type for client list requests
    RegisterRequest,
    RegisterResponse,
    LoginRequest,
    LoginResponse,
    ChatRequest,   // Request to join chat
    ChatResponse,   // Response to join chat
    ClientInfoRequest,   // Request for client information
    ClientInfoResponse, // Response with client information
    ChatHistoryRequest,   // Request for chat history
    ChatHistoryResponse,
    GlobalMessage,           // Message for global chat
    GlobalChatHistoryRequest, // Request for global chat history
    GlobalChatHistoryResponse // Response for global chat history
};

namespace olc
{
    namespace net
    {
        // Template class for server interface that handles client connections and messaging
        template<typename T>
        class server_interface
        {
        public:
            // Utility method to find a client by their unique ID
            std::shared_ptr<olc::net::connection<CustomMsgTypes>> getClientByID(uint32_t id) {
                for (auto& client : getAllClients()) {
                    if (client && client->getID() == id) {
                        return client;
                    }
                }
                return nullptr;
            }

            // Constructor: Initialize server with specified port
            server_interface(uint16_t port)
                : m_asioAcceptor(m_asioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
            {
            }

            // Destructor: Ensure proper cleanup when server is destroyed
            virtual ~server_interface()
            {
                stop();
            }

            // Virtual method called when a client is validated and ready to communicate
            virtual void onClientValidated(std::shared_ptr<connection<T>> client)
            {
                std::cout << "[SERVER] Client " << client->getID() << " validated" << std::endl;
            }

            // Start the server and begin accepting client connections
            bool start()
            {
                try
                {
                    // Begin waiting for client connections
                    waitForClientConnection();

                    // Start ASIO context in a separate thread
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

            // Stop the server and clean up resources
            void stop()
            {
                // Stop ASIO context
                m_asioContext.stop();

                // Wait for context thread to finish
                if (m_threadContext.joinable())
                    m_threadContext.join();

                std::cout << "[SERVER] Stopped!\n";
            }

        public:
            // Method to safely remove a client from the server
            void removeClient(std::shared_ptr<connection<T>> client)
            {
                if (client)
                {
                    std::cout << "[SERVER] Removing client: ID=" << client->getID() << std::endl;

                    // Call disconnect handler before removing
                    onClientDisconnect(client);

                    // Disconnect the client
                    client->disconnect();

                    // Remove client from the connections list
                    m_deqConnections.erase(
                        std::remove_if(m_deqConnections.begin(), m_deqConnections.end(),
                            [&client](const std::shared_ptr<connection<T>>& conn) {
                                return conn == client || !conn || !conn->isConnected();
                            }),
                        m_deqConnections.end());

                    // Log current number of active connections
                    std::cout << "[SERVER] Active connections remaining: " << m_deqConnections.size() << std::endl;
                }
            }

            // ASYNC - Start listening for new client connections
            void waitForClientConnection()
            {
                m_asioAcceptor.async_accept(
                    [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
                    {
                        if (!ec)
                        {
                            std::cout << "[SERVER] New Connection: " << socket.remote_endpoint() << "\n";

                            // Create new connection object
                            std::shared_ptr<connection<T>> newconn =
                                std::make_shared<connection<T>>(connection<T>::owner::server,
                                    m_asioContext,
                                    std::move(socket),
                                    m_qMessagesIn);

                            // Check if connection should be accepted
                            if (onClientConnect(newconn))
                            {
                                m_deqConnections.push_back(std::move(newconn));

                                // Start reading messages from the new client
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

                        // Continue listening for more connections recursively
                        waitForClientConnection();
                    });
            }

            // Send a message to a specific client with connection validation
            void messageClient(std::shared_ptr<connection<T>> client, const message<T>& msg)
            {
                if (client && client->isConnected())
                {
                    client->send(msg);
                }
                else
                {
                    // Remove client if connection is invalid
                    removeClient(client);
                }
            }

            // Broadcast a message to all connected clients with optional exclusion
            void messageAllClients(const message<T>& msg, std::shared_ptr<connection<T>> pIgnoreClient = nullptr)
            {
                std::vector<std::shared_ptr<connection<T>>> invalidClients;

                for (auto& client : m_deqConnections)
                {
                    if (client && client->isConnected())
                    {
                        if (client != pIgnoreClient)
                            client->send(msg);
                    }
                    else
                    {
                        // Collect invalid connections for later removal
                        invalidClients.push_back(client);
                    }
                }

                // Remove all invalid connections
                for (auto& client : invalidClients)
                {
                    removeClient(client);
                }
            }

            // Process incoming messages from the message queue
            void update(size_t maxMessages = -1, bool wait = false)
            {
                if (wait && m_qMessagesIn.empty())
                {
                    // Wait for messages if queue is empty and wait flag is set
                    m_qMessagesIn.wait();
                }

                size_t messageCount = 0;
                while (messageCount < maxMessages && !m_qMessagesIn.empty())
                {
                    // Get message from front of queue
                    auto msg = m_qMessagesIn.front();
                    m_qMessagesIn.pop_front();

                    // Process the message
                    onMessage(msg.remote, msg.msg);

                    messageCount++;
                }
            }

            // Get reference to all connected clients (used by CustomServer::getClientByID)
            std::deque<std::shared_ptr<connection<T>>>& getAllClients()
            {
                return m_deqConnections;
            }

        protected:
            // Virtual methods that should be overridden by derived classes

            // Called when a new client attempts to connect - return true to accept
            virtual bool onClientConnect(std::shared_ptr<connection<T>> client)
            {
                return false;
            }

            // Called when a client disconnects - cleanup client-specific resources
            virtual void onClientDisconnect(std::shared_ptr<connection<T>> client)
            {
            }

            // Called when a message is received from a client - implement message handling logic
            virtual void onMessage(std::shared_ptr<connection<T>> client, message<T>& msg)
            {
            }

        protected:
            // Thread-safe queue for incoming messages from clients
            tsQueue<owned_message<T>> m_qMessagesIn;

            // ASIO context for handling I/O operations
            boost::asio::io_context m_asioContext;
            std::thread m_threadContext;

            // TCP acceptor for listening to new connections
            boost::asio::ip::tcp::acceptor m_asioAcceptor;

            // Container storing all active client connections
            std::deque<std::shared_ptr<connection<T>>> m_deqConnections;

            // Counter for assigning unique IDs to clients
            uint32_t nIDCounter = 10000;
        };
    }
}