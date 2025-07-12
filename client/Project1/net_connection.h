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

        // Template class representing a network connection between client and server
        // Handles message sending/receiving and connection validation
        template<typename T>
        class connection : public std::enable_shared_from_this<connection<T>>
        {
        public:
            // Enumeration to identify whether this connection belongs to server or client
            enum class owner
            {
                server,
                client
            };

            // Constructor that initializes connection with owner type, IO context, and incoming message queue
            // Sets up handshake validation data for server connections
            connection(owner parent, boost::asio::io_context& asioContext, tsQueue<owned_message<T>>& qIn)
                : m_asioContext(asioContext), m_socket(asioContext), m_qMessageIn(qIn)
            {
                m_nOwnerType = parent;

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

            // Returns the unique identifier of this connection
            uint32_t getID() const
            {
                return id;
            }

            // Establishes connection to a client (server-side)
            // Assigns unique ID and initiates handshake validation process
            void connectToClient(olc::net::server_interface<T> * server, uint32_t uid = 0)
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

            // Connects to server using provided endpoints (client-side)
            // Returns true if connection attempt was initiated successfully
            bool connectToServer(const boost::asio::ip::tcp::resolver::results_type& endpoints)
            {
                if (m_nOwnerType == owner::client)
                {
                    // Asynchronously connect to server using provided endpoints
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

            // Closes the connection by posting a close operation to the IO context
            bool disconnect()
            {
                if (isConnected())
                {
                    boost::asio::post(m_asioContext, [this]() { m_socket.close(); });
                }
                return true;
            }

            // Checks if the socket is currently open and connected
            bool isConnected() const
            {
                return m_socket.is_open();
            }

            // Sends a message by adding it to the outgoing message queue
            // Initiates write operation if no other message is currently being sent
            bool send(const message<T>& msg)
            {
                boost::asio::post(m_asioContext,
                    [this, msg]()
                    {
                        bool writingMessage = !m_qMessageOut.empty();

                        // Add new message to outgoing queue
                        message<T> msgCopy = msg;

                        m_qMessageOut.push_back(msgCopy);
                        if (!writingMessage)
                        {
                            writeHeader();
                        }
                    });
                return true;
            }

        protected:
            // Asynchronously writes message header to the socket
            // Continues with body writing if message has body data
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
            // Removes completed message from queue and continues with next message if available
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

        private:
            // Asynchronously reads message header from the socket
            // Determines if message has body and continues reading accordingly
            void ReadHeader()
            {
                // Reset temporary message for new incoming message
                m_tempMsg = message<T>();

                // Read message header asynchronously
                boost::asio::async_read(m_socket,
                    boost::asio::buffer(&m_tempMsg.header, sizeof(messageHeader<T>)),
                    [this](std::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            if (m_tempMsg.header.size > 0)
                            {
                                // Calculate body size and read body if it exists
                                size_t bodySize = m_tempMsg.header.size - sizeof(messageHeader<T>);
                                if (bodySize > 0) {
                                    m_tempMsg.body.resize(bodySize);
                                    ReadBody();
                                }
                                else {
                                    // Message has no body, add to incoming queue
                                    AddToIncomingMessageQueue();
                                }
                            }
                            else
                            {
                                // If message body is empty, add message to queue
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
            // Adds completed message to incoming queue
            void ReadBody()
            {
                // Read message body data asynchronously
                boost::asio::async_read(m_socket,
                    boost::asio::buffer(m_tempMsg.body.data(), m_tempMsg.body.size()),
                    [this](boost::system::error_code ec, std::size_t length)
                    {
                        if (!ec)
                        {
                            AddToIncomingMessageQueue();
                        }
                        else
                        {
                            std::cerr << "[" << id << "] Read Body Failed: " << ec.message() << std::endl;
                            m_socket.close();
                        }
                    });
            }

            // Adds the completed message to the incoming message queue
            // Handles ownership information differently for server and client connections
            void AddToIncomingMessageQueue()
            {
                // Add message to incoming queue with proper ownership information
                // Server connections include connection reference, client connections don't
                if (m_nOwnerType == owner::server)
                {
                    m_qMessageIn.push_back({ this->shared_from_this(), m_tempMsg });
                }
                else
                {
                    // Client connections add message without connection reference
                    m_qMessageIn.push_back({ nullptr, m_tempMsg });
                }

                // Continue reading next message after adding current one to queue
                ReadHeader();
            }

            // Encrypts/scrambles input data using XOR operations and bit shifting
            // Used for handshake validation to prevent unauthorized connections
            uint64_t scramble(uint64_t nInput)
            {
                uint64_t out = nInput ^ 0xDEADBEEFC0DECAFE;
                out = (out & 0xF0F0F0F0F0F0F0) >> 4 | (out & 0xF0F0F0F0F0F0F0) << 4;
                return out ^ 0xC0DEFACE12345678;
            }

            // Writes handshake validation data to establish secure connection
            // Initiates message reading for client connections after successful validation
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

            // Reads and validates handshake data from the remote connection
            // Verifies client authenticity on server side, responds to challenge on client side
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
                                    std::cout << "Client Validated\n";
                                    if (server) {  // Ensure server pointer is not null
                                        server->onClientValidated(this->shared_from_this());
                                    }
                                    ReadHeader();
                                }
                                else
                                {
                                    std::cout << "Client Disconnected (Fail Validation)\n";
                                    m_socket.close();
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
                            std::cout << "Client Disconnected (ReadValidation)\n";
                            m_socket.close();
                        }
                    });
            }

        protected:
            // TCP socket for network communication
            boost::asio::ip::tcp::socket m_socket;
            boost::asio::io_context& m_asioContext;
            
            // Temporary message storage for incoming messages during reading
            message<T> m_tempMsg;

            // Thread-safe queue for outgoing messages
            tsQueue<message<T>> m_qMessageOut;
            // Reference to shared incoming message queue
            tsQueue<owned_message<T>>& m_qMessageIn;
            // Identifies whether this connection belongs to server or client
            owner m_nOwnerType = owner::server;
            // Unique identifier for this connection
            uint32_t id = 0;

            // Handshake validation data for secure connection establishment
            uint64_t m_nHandshakeOut = 0;
            uint64_t m_nHandshakeIn = 0;
            uint64_t m_nHandshakeCheck = 0;
        };
    }
}