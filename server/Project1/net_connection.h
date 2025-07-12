#pragma once
#include "net_common.h"
#include "net_tsQueue.h"
#include "net_message.h"

namespace olc
{
    namespace net
    {
        template<typename T>
        class server_interface;

        // Template class representing a network connection that can be either client or server side
        template<typename T>
        class connection : public std::enable_shared_from_this<connection<T>>
        {
        public:
            // Enumeration to specify whether this connection belongs to server or client
            enum class owner
            {
                server,
                client
            };

            // Constructor for client connections (3 parameters)
            // Used when creating a connection from client side
            connection(owner parent, boost::asio::io_context& asioContext, tsQueue<owned_message<T>>& qIn)
                : m_asioContext(asioContext), m_socket(asioContext), m_qMessageIn(qIn)
            {
                m_nOwnerType = parent;

                // Initialize handshake data for server connections
                if (m_nOwnerType == owner::server)
                {
                    m_nHandshakeOut = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());
                    m_nHandshakeCheck = scramble(m_nHandshakeOut);
                }
                else
                {
                    m_nHandshakeIn = 0;
                    m_nHandshakeOut = 0;
                }
            }

            // Constructor for server connections (4 parameters)
            // Used when server accepts a new client connection
            connection(owner parent, boost::asio::io_context& asioContext, boost::asio::ip::tcp::socket socket,
                tsQueue<owned_message<T>>& qIn)
                : m_asioContext(asioContext), m_socket(std::move(socket)), m_qMessageIn(qIn)
            {
                m_nOwnerType = parent;

                // Initialize handshake data for server connections
                if (m_nOwnerType == owner::server)
                {
                    m_nHandshakeOut = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());
                    m_nHandshakeCheck = scramble(m_nHandshakeOut);
                }
                else
                {
                    m_nHandshakeIn = 0;
                    m_nHandshakeOut = 0;
                }
            }

            virtual ~connection() {}

            // Returns the unique ID of this connection
            uint32_t getID() const
            {
                return id;
            }

            // Returns reference to the underlying TCP socket
            boost::asio::ip::tcp::socket& socket()
            {
                return m_socket;
            }

            // Initiates connection process for a client connecting to server
            // Starts handshake validation sequence
            void connectToClient(olc::net::server_interface<T>* server, uint32_t uid = 0)
            {
                if (m_nOwnerType == owner::server)
                {
                    if (m_socket.is_open())
                    {
                        id = uid;

                        // ReadHeader();
                        WriteValidation();
                        ReadValidation(server);
                    }
                }
            }

            // Connects to server using provided endpoints
            bool connectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints)
            {
                if (m_nOwnerType == owner::client)
                {
                    // Attempt asynchronous connection to server
                    boost::asio::async_connect(m_socket, endpoints,
                        [this](boost::system::error_code ec, boost::asio::ip::tcp::endpoint endpoint)
                        {
                            if (!ec)
                            {
                                // Start reading messages from the server
                                // ReadHeader();
                                ReadValidation();
                            }
                        });
                }
                return true;
            }

            // Disconnects the connection by closing the socket
            bool disconnect()
            {
                if (isConnected())
                {
                    boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
                }
                return true;
            }

            // Checks if the connection is still active
            bool isConnected() const
            {
                return m_socket.is_open() && !m_isRemoved;
            }

            // Sends a message through this connection
            // Adds message to outgoing queue and triggers write if needed
            bool send(const message<T>& msg)
            {
                boost::asio::post(m_asioContext,
                    [this, msg]()
                    {
                        bool writingMessage = !m_qMessageOut.empty();

                        // Create a copy of the message for sending
                        message<T> msgCopy = msg;

                        m_qMessageOut.push_back(msgCopy);
                        if (!writingMessage)
                        {
                            writeHeader();
                        }
                    });
                return true;
            }

        private:
            // Validates username according to specified rules
            bool validateUsername(const std::string& username, std::string& errorMsg) {
                // Check minimum and maximum length
                if (username.length() < 3) {
                    errorMsg = "Username must contain at least 3 characters";
                    return false;
                }

                if (username.length() > 20) {
                    errorMsg = "Username must not exceed 20 characters";
                    return false;
                }

                // Check for allowed characters (letters, numbers, underscore)
                for (char c : username) {
                    if (!std::isalnum(c) && c != '_') {
                        errorMsg = "Username can only contain letters, numbers and underscore";
                        return false;
                    }
                }

                // Username must not start with a number
                if (std::isdigit(username[0])) {
                    errorMsg = "Username must not start with a number";
                    return false;
                }

                // Check for forbidden usernames
                std::vector<std::string> forbiddenNames = { "admin", "administrator", "root", "system", "server" };
                std::string lowercaseUsername = username;
                std::transform(lowercaseUsername.begin(), lowercaseUsername.end(), lowercaseUsername.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                for (const auto& name : forbiddenNames) {
                    // If username matches one of forbidden names, reject it (except "admin")
                    if (name == lowercaseUsername && name != "admin") {
                        errorMsg = "This username is reserved by system";
                        return false;
                    }
                }

                return true;
            }

            // Validates password according to specified security rules
            bool validatePassword(const std::string& password, std::string& errorMsg) {
                // Check minimum length
                if (password.length() < 6) {
                    errorMsg = "Password must contain at least 6 characters";
                    return false;
                }

                // Check maximum length
                if (password.length() > 64) {
                    errorMsg = "Password must not exceed 64 characters";
                    return false;
                }

                // Check for at least one digit
                bool hasDigit = false;
                for (char c : password) {
                    if (std::isdigit(c)) {
                        hasDigit = true;
                        break;
                    }
                }

                if (!hasDigit) {
                    errorMsg = "Password must contain at least one digit";
                    return false;
                }

                // Check for at least one letter
                bool hasLetter = false;
                for (char c : password) {
                    if (std::isalpha(c)) {
                        hasLetter = true;
                        break;
                    }
                }

                if (!hasLetter) {
                    errorMsg = "Password must contain at least one letter";
                    return false;
                }

                return true;
            }

        protected:
            // Asynchronously writes message header to the socket
            void writeHeader()
            {
                boost::asio::async_write(m_socket,
                    boost::asio::buffer(&m_qMessageOut.front().header, sizeof(messageHeader<T>)),
                    [this](boost::system::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            if (m_qMessageOut.front().body.size() > 0)
                            {
                                writeBody();
                            }
                            else
                            {
                                m_qMessageOut.pop_front();
                                if (!m_qMessageOut.empty())
                                {
                                    writeHeader();
                                }
                            }
                        }
                        else
                        {
                            std::cerr << "[" << this << "] Write Header Failed: " << ec.message() << std::endl;
                            m_socket.close();
                        }
                    });
            }

            // Asynchronously writes message body to the socket
            void writeBody()
            {
                boost::asio::async_write(m_socket,
                    boost::asio::buffer(m_qMessageOut.front().body.data(), m_qMessageOut.front().body.size()),
                    [this](std::error_code ec, std::size_t length)
                    // boost::system::error_code ec
                    {
                        if (!ec)
                        {
                            m_qMessageOut.pop_front();
                            if (!m_qMessageOut.empty())
                            {
                                writeHeader();
                            }
                        }
                        else
                        {
                            std::cerr << "[" << this << "] Write Body Failed: " << ec.message() << std::endl;
                            m_socket.close();
                        }
                    });
            }

            protected:
                // Flag to track if connection has been marked for removal
                bool m_isRemoved = false;

        public:
                // Safely removes client connection from server
                void removeClient(olc::net::server_interface<T>* server)
                {
                    if (m_nOwnerType == owner::server && server)
                    {
                        // Close connection if it's still open
                        if (isConnected())
                        {
                            std::cout << "[" << id << "] Client disconnected, closing connection" << std::endl;

                            // Schedule socket closure through asio for thread safety
                            boost::asio::post(m_asioContext, [this]() {
                                m_socket.close();
                                });

                            // Clear outgoing message queue
                            while (!m_qMessageOut.empty())
                                m_qMessageOut.pop_front();

                            // Mark connection as removed (could be useful for cleanup logic)
                            // m_isRemoved = true;  // This flag might be used later
                        }
                    }
                }


        private:
            // Asynchronously reads message header from the socket
            void ReadHeader()
            {
                // Initialize temporary message for incoming data
                m_tempMsg = message<T>();

                // Use weak_ptr and lock for safe shared_ptr handling
                boost::asio::async_read(m_socket,
                    boost::asio::buffer(&m_tempMsg.header, sizeof(messageHeader<T>)),
                    [this](std::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            std::cout << "[" << id << "] Received header, ID=" << static_cast<uint32_t>(m_tempMsg.header.id)
                                << ", Size=" << m_tempMsg.header.size << std::endl;

                            if (m_tempMsg.header.size > 0)
                            {
                                // Calculate body size: total message size minus header size
                                size_t bodySize = m_tempMsg.header.size - sizeof(messageHeader<T>);
                                if (bodySize > 0) {
                                    m_tempMsg.body.resize(bodySize);
                                    ReadBody();
                                }
                                else {
                                    // Message has no body, process immediately
                                    AddToIncomingMessageQueue();
                                }
                            }
                            else
                            {
                                // If message body is empty, add message to queue immediately
                                AddToIncomingMessageQueue();
                            }
                        }
                        else
                        {
                            std::cerr << "[" << id << "] Read Header Failed: " << ec.message() << std::endl;
                            m_socket.close();
                        }
                    });
            }

            // Asynchronously reads message body from the socket
            void ReadBody()
            {
                // Don't use shared_from_this() here
                boost::asio::async_read(m_socket,
                    boost::asio::buffer(m_tempMsg.body.data(), m_tempMsg.body.size()),
                    [this](boost::system::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            std::cout << "[" << id << "] Received body, Size=" << length << std::endl;
                            AddToIncomingMessageQueue();
                        }
                        else
                        {
                            std::cerr << "[" << id << "] Read Body Failed: " << ec.message() << std::endl;
                            m_socket.close();
                        }
                    });
            }

            // Adds complete message to incoming message queue
            void AddToIncomingMessageQueue()
            {
                // Create ownership info and add message to queue
                std::cout << "[" << id << "] Adding message to queue, ID=" << static_cast<int>(m_tempMsg.header.id) << std::endl;

                // If we're server, attach connection info to message
                if (m_nOwnerType == owner::server)
                {
                    m_qMessageIn.push_back({ this->shared_from_this(), m_tempMsg });
                }
                else
                {
                    // If we're client, add message without connection info
                    m_qMessageIn.push_back({ nullptr, m_tempMsg });
                }

                // After adding message to queue, start reading next message
                ReadHeader();
            }

            // Encrypts data using simple scrambling algorithm
            uint64_t scramble(uint64_t nInput)
            {
                uint64_t out = nInput ^ 0xDEADBEEFC0DECAFE;
                out = (out & 0xF0F0F0F0F0F0F0) >> 4 | (out & 0xF0F0F0F0F0F0F0) << 4;
                return out ^ 0xC0DEFACE12345678;
            }

            // Sends handshake validation data to remote endpoint
            void WriteValidation()
            {
                boost::asio::async_write(m_socket, boost::asio::buffer(&m_nHandshakeOut, sizeof(uint64_t)),
                    [this](std::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            if (m_nOwnerType == owner::client)
                                ReadHeader();
                        }
                        else
                        {
                            m_socket.close();
                        }
                    }
                );
            }

            // Reads and validates handshake data from remote endpoint
            void ReadValidation(olc::net::server_interface<T>* server = nullptr)
            {
                boost::asio::async_read(m_socket, boost::asio::buffer(&m_nHandshakeIn, sizeof(uint64_t)),
                    [this, server](std::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            if (m_nOwnerType == owner::server)
                            {
                                if (m_nHandshakeIn == m_nHandshakeCheck)
                                {
                                    std::cout << "Client successfully validated\n";
                                    if (server) {  // Ensure server is not null
                                        server->onClientValidated(this->shared_from_this());
                                    }
                                    ReadHeader();
                                }
                                else
                                {
                                    std::cout << "Client validation failed (handshake mismatch)\n";
                                    // Use removeClient method instead of direct socket closure
                                    if (server) {
                                        removeClient(server);
                                    }
                                    else {
                                        m_socket.close();
                                    }
                                }
                            }
                            else
                            {
                                m_nHandshakeOut = scramble(m_nHandshakeIn);
                                WriteValidation();
                            }
                        }
                        else
                        {
                            std::cout << "Client validation failed (ReadValidation error)\n";
                            // Use removeClient method instead of direct socket closure
                            if (m_nOwnerType == owner::server && server) {
                                removeClient(server);
                            }
                            else {
                                m_socket.close();
                            }
                        }
                    });
            }

        protected:
            // Core networking components
            boost::asio::ip::tcp::socket m_socket;
            boost::asio::io_context& m_asioContext;

            // Temporary message storage for incoming data
            message<T> m_tempMsg;

            // Thread-safe queue for outgoing messages
            tsQueue<message<T>> m_qMessageOut;
            // Reference to shared incoming message queue
            tsQueue<owned_message<T>>& m_qMessageIn;
            // Specifies whether this connection belongs to server or client
            owner m_nOwnerType = owner::server;
            // Unique identifier for this connection
            uint32_t id = 0;

            // Handshake validation data
            uint64_t m_nHandshakeOut = 0;
            uint64_t m_nHandshakeIn = 0;
            uint64_t m_nHandshakeCheck = 0;

            friend class server_interface<T>;
        };
    }
}