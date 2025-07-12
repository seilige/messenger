#pragma once
#include "net_common.h"
#include "net_tsQueue.h"
#include "net_message.h"
#include "net_connection.h"
#include <vector>
#include <chrono>  // Added include for time handling

// Enumeration defining all custom message types used in client-server communication
enum class CustomMsgTypes : uint32_t
{
    ServerAccept,           // Server accepts client connection
    ServerDeny,             // Server denies client connection
    ServerPing,             // Server ping message for keep-alive
    MessageAll,             // Broadcast message to all clients
    ServerMessage,          // General server message
    KeyPress,               // Message type for key press events
    DirectMessage,          // Message type for direct messages between clients
    RequestClientList,      // Request for list of connected clients
    RegisterRequest,        // User registration request
    RegisterResponse,       // Server response to registration
    LoginRequest,           // User login request
    LoginResponse,          // Server response to login
    ChatRequest,            // Request to start chat session
    ChatResponse,           // Response to chat request
    ClientInfoRequest,      // Request for client information
    ClientInfoResponse,     // Response with client information
    ChatHistoryRequest,     // Request for chat history
    ChatHistoryResponse,    // Response with chat history
    GlobalMessage,          // Global message broadcast
    GlobalChatHistoryRequest,  // Request for global chat history
    GlobalChatHistoryResponse  // Response with global chat history
};

// Maximum allowed message size in bytes
const size_t MAX_MESSAGE_SIZE = 8192;

/**
 * Displays the client menu interface
 * @param isAuthenticated - whether the user is logged in or not
 */
void DisplayMenu(bool isAuthenticated)
{
    std::cout << "\n????????????????????????????" << std::endl;
    std::cout << "?        Client Menu          ?" << std::endl;
    std::cout << "????????????????????????????" << std::endl;
    std::cout << "? D - send direct message     ?" << std::endl;
    std::cout << "? C - start chat with user    ?" << std::endl;
    if (isAuthenticated) {
        std::cout << "? G - send global message     ?" << std::endl;
        std::cout << "? H - global chat history     ?" << std::endl;
        std::cout << "? I - user information        ?" << std::endl;
    }
    else {
        std::cout << "? R - registration            ?" << std::endl;
        std::cout << "? L - login                   ?" << std::endl;
    }
    std::cout << "? Q - exit                    ?" << std::endl;
    std::cout << "????????????????????????????" << std::endl;
}

namespace olc
{
    namespace net
    {
        /**
         * Template class for client interface implementation
         * @tparam T - message type enumeration
         */
        template<typename T>
        class client_interface
        {
        public:
            /**
             * Structure to hold client information
             */
            struct ClientInfo {
                uint32_t id;                                    // Unique client identifier
                std::string status;                             // Client status (Online, Offline, etc.)
                std::chrono::system_clock::time_point lastSeen; // Last time client was seen
            };

            uint32_t m_myID = 0; // Personal ID of this client instance

            /**
             * Constructor - initializes the client interface
             */
            client_interface() : m_socket(m_context)
            {
            }

            /**
             * Destructor - ensures proper cleanup
             */
            virtual ~client_interface()
            {
                disconnect();
            }

            /**
             * Connects to the server
             * @param host - server hostname or IP address
             * @param port - server port number
             * @return true if connection successful, false otherwise
             */
            bool Connect(const std::string& host, const uint16_t port)
            {
                try
                {
                    // Create connection object with proper constructor parameters
                    m_connection = std::make_unique<connection<T>>(connection<T>::owner::client,
                        m_context,
                        m_qMessageIn);

                    // Resolve hostname to IP address
                    boost::asio::ip::tcp::resolver resolver(m_context);
                    boost::asio::ip::tcp::resolver::results_type endpoints =
                        resolver.resolve(host, std::to_string(port));

                    // Connect to server
                    m_connection->connectToServer(endpoints);

                    // Start context thread for handling asynchronous operations
                    thrContext = std::thread([this]() {m_context.run(); });
                }
                catch (std::exception& e)
                {
                    std::cerr << "client exception: " << e.what() << '\n';
                    return false;
                }

                return true;
            }

            /**
             * Checks if client is authenticated
             * @return authentication status
             */
            bool isAuthenticated() const { return false; }

            /**
             * Displays client information in a formatted table
             * @param clientID - client's unique identifier
             * @param username - client's username
             * @param status - client's current status
             */
            void DisplayClientInfo(uint32_t clientID, const std::string& username, const std::string& status)
            {
                std::cout << "\n???????????????????????????????????????????" << std::endl;
                std::cout << "?          Client Information           ?" << std::endl;
                std::cout << "???????????????????????????????????????????" << std::endl;
                std::cout << "? ID:        " << std::setw(26) << std::left << clientID << " ?" << std::endl;
                std::cout << "? Username:  " << std::setw(26) << std::left << username << " ?" << std::endl;
                std::cout << "? Status:    " << std::setw(26) << std::left << status << " ?" << std::endl;
                std::cout << "???????????????????????????????????????????" << std::endl;
            }

            /**
             * Displays list of connected clients with current client highlighted
             * @param clientList - string containing client list data
             * @param m_knownClients - vector to store parsed client information
             * @param m_myID - current client's ID for highlighting
             */
            void DisplayConnectedClients(const std::string& clientList, std::vector<ClientInfo>& m_knownClients, uint32_t m_myID)
            {
                // Parse the client list string into structured data
                ParseClientList(clientList, m_knownClients);

                // Find and highlight current client in the list
                bool foundYou = false;
                for (const auto& client : m_knownClients) {
                    if (client.id == m_myID) {
                        foundYou = true;
                        std::cout << "? #" << std::setw(11) << std::left << client.id
                            << " ? " << std::setw(21) << std::left << "YOU (ONLINE)"
                            << " ?" << std::endl;
                        break;
                    }
                }

                // Warning if current client not found in server's list
                if (!foundYou) {
                    std::cout << "? WARNING: Your client ID not found in list!" << std::endl;
                }
            }

            /**
             * Parses client list string and populates client information vector
             * @param clientList - raw string containing client list from server
             * @param m_knownClients - vector to populate with parsed client data
             */
            void ParseClientList(const std::string& clientList, std::vector<ClientInfo>& m_knownClients)
            {
                // Clear existing client data
                m_knownClients.clear();

                // Look for the standard client list format: "Connected clients: #10000, #10001, #10002"
                size_t startPos = clientList.find("Connected clients:");
                if (startPos == std::string::npos) {
                    std::cout << "DEBUG: Failed to find 'Connected clients:' in message" << std::endl;
                    std::cout << "Message content: " << clientList << std::endl;
                    return;
                }

                // Extract the client IDs portion after the header
                startPos += 18; // Length of "Connected clients:"
                std::string clients = clientList.substr(startPos);

                // Parse comma-separated client IDs
                std::stringstream ss(clients);
                std::string token;

                while (std::getline(ss, token, ',')) {
                    // Remove leading and trailing whitespace
                    token.erase(0, token.find_first_not_of(" \t"));
                    token.erase(token.find_last_not_of(" \t") + 1);

                    // Find the hash symbol that prefixes client IDs
                    size_t hashPos = token.find('#');
                    if (hashPos != std::string::npos) {
                        try {
                            // Extract numeric client ID after the hash
                            uint32_t id = std::stoul(token.substr(hashPos + 1));
                            std::cout << "DEBUG: Found client ID: " << id << std::endl;

                            // Add client to the known clients list with default status
                            m_knownClients.push_back({ id, "Online", std::chrono::system_clock::now() });
                        }
                        catch (const std::exception& e) {
                            // Skip malformed client IDs
                            std::cout << "DEBUG: Failed to parse client ID from: " << token << std::endl;
                        }
                    }
                }
            }
            // Login interface for user authentication
            void LoginUserInterface()
            {
                if (isAuthenticated()) {
                    std::cout << "You are already logged into the system" << std::endl;
                    return;
                }

                std::string username, password;

                std::cout << "\nLogin to system" << std::endl;
                std::cout << "Enter username: ";
                std::getline(std::cin, username);

                std::cout << "Enter password: ";
                std::getline(std::cin, password);

                if (LoginUser(username, password)) {
                    std::cout << "Request sent to server, please wait..." << std::endl;
                }
                else {
                    std::cout << "Error sending request" << std::endl;
                }
            }

            // Send login request to server with username and password
            bool LoginUser(const std::string& username, const std::string& password)
            {
                if (!isConnected()) {
                    std::cout << "Error: no connection to server" << std::endl;
                    return false;
                }

                if (username.empty() || password.empty()) {
                    std::cout << "Error: username and password cannot be empty" << std::endl;
                    return false;
                }

                olc::net::message<CustomMsgTypes> msg;
                msg.header.id = CustomMsgTypes::LoginRequest;

                // Pack username size and characters into message
                uint32_t usernameSize = static_cast<uint32_t>(username.size());
                msg << usernameSize;
                for (const char& c : username) {
                    msg << c;
                }

                // Pack password size and characters into message
                uint32_t passwordSize = static_cast<uint32_t>(password.size());
                msg << passwordSize;
                for (const char& c : password) {
                    msg << c;
                }

                std::cout << "Sending login request for user: " << username << std::endl;
                return send(msg);
            }

            // Request list of connected clients from server
            void RequestClientList()
            {
                if (!isConnected()) {
                    std::cout << "Error: Not connected to server" << std::endl;
                    return;
                }

                olc::net::message<CustomMsgTypes> msg;
                msg.header.id = CustomMsgTypes::RequestClientList;
                send(msg);
                std::cout << "Requesting client list from server..." << std::endl;
            }

            // User registration interface
            void RegisterUserInterface(uint32_t m_myID)
            {
                if (isAuthenticated()) {
                    std::cout << "\nYou are already registered in the system" << std::endl;
                    std::cout << "Your ID: " << m_myID << std::endl;
                    return;
                }

                std::string username, password, email;

                std::cout << "\nUser registration" << std::endl;
                std::cout << "Enter username: ";
                std::getline(std::cin, username);

                std::cout << "Enter password: ";
                std::getline(std::cin, password);

                std::cout << "Enter email: ";
                std::getline(std::cin, email);

                if (RegisterUser(username, password, email)) {
                    std::cout << "Request sent to server, please wait..." << std::endl;
                }
                else {
                    std::cout << "Error sending request" << std::endl;
                }
            }

            // Send registration request to server with user credentials
            bool RegisterUser(const std::string& username, const std::string& password, const std::string& email)
            {
                if (!isConnected()) {
                    std::cout << "Error: no connection to server" << std::endl;
                    return false;
                }

                if (username.empty() || password.empty() || email.empty()) {
                    std::cout << "Error: username, password and email cannot be empty" << std::endl;
                    return false;
                }

                olc::net::message<CustomMsgTypes> msg;
                msg.header.id = CustomMsgTypes::RegisterRequest;

                // Pack username size and characters into message
                uint32_t usernameSize = static_cast<uint32_t>(username.size());
                msg << usernameSize;
                for (const char& c : username) {
                    msg << c;
                }

                // Pack password size and characters into message
                uint32_t passwordSize = static_cast<uint32_t>(password.size());
                msg << passwordSize;
                for (const char& c : password) {
                    msg << c;
                }

                // Pack email size and characters into message
                uint32_t emailSize = static_cast<uint32_t>(email.size());
                msg << emailSize;
                for (const char& c : email) {
                    msg << c;
                }

                std::cout << "Sending registration request for user: " << username << std::endl;
                return send(msg);
            }
         
            // Display formatted private message with sender info and timestamp
            void DisplayPrivateMessage(uint32_t senderID, const std::string& message) {
                std::cout << "\n===============================================" << std::endl;
                std::cout << "         PRIVATE MESSAGE RECEIVED" << std::endl;
                std::cout << "===============================================" << std::endl;
                std::cout << "From: Client #" << senderID << std::endl;
                std::cout << "Time: " << GetCurrentTimeString() << std::endl;
                std::cout << "-----------------------------------------------" << std::endl;

                // Split long message into lines for better readability
                const size_t maxLineLength = 43; // Maximum line length in characters
                size_t pos = 0;

                while (pos < message.length()) {
                    size_t lineEnd = pos + maxLineLength;
                    if (lineEnd > message.length()) {
                        lineEnd = message.length();
                    }

                    // If line ends in middle of word, break at last space
                    if (lineEnd < message.length() && message[lineEnd] != ' ') {
                        size_t lastSpace = message.find_last_of(' ', lineEnd);
                        if (lastSpace != std::string::npos && lastSpace > pos) {
                            lineEnd = lastSpace;
                        }
                    }

                    std::string line = message.substr(pos, lineEnd - pos);
                    std::cout << "| " << std::setw(41) << std::left << line << " |" << std::endl;

                    pos = lineEnd;
                    if (pos < message.length() && message[pos] == ' ') {
                        pos++; // Skip space at beginning of next line
                    }
                }

                std::cout << "===============================================" << std::endl;
                std::cout << "Press 'D' to reply or any other key to continue" << std::endl;
            }
            

            // Get current time as formatted string (HH:MM:SS)
            std::string GetCurrentTimeString() {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);

                std::tm tm = {};
                localtime_s(&tm, &time_t);  // Thread-safe version for Windows

                std::stringstream ss;
                ss << std::put_time(&tm, "%H:%M:%S");
                return ss.str();
            }

            // Request client information from server
            bool RequestClientInfo()
            {
                if (!isConnected()) {
                    std::cout << "Error: no connection to server" << std::endl;
                    return false;
                }

                // Create message to request client information
                olc::net::message<CustomMsgTypes> msg;
                msg.header.id = CustomMsgTypes::ClientInfoRequest;
                msg << m_myID;  // Include our ID to get information about ourselves

                std::cout << "Requesting client info for ID #" << m_myID << std::endl;
                return send(msg);
            }

            // Disconnect from server and clean up resources
            void disconnect()
            {
                if (isConnected())
                {
                    m_connection->disconnect();
                }

                // Stop io_context
                m_context.stop();

                // Wait for context thread to finish if it's joinable
                if (thrContext.joinable())
                    thrContext.join();

                // Reset connection pointer
                m_connection.reset();
            }

            // Check if client is connected to server
            bool isConnected()
            {
                if (m_connection)
                    return m_connection->isConnected();
                else
                    return false;
            }

            // Get reference to incoming message queue
            tsQueue<owned_message<T>>& incoming()
            {
                return m_qMessageIn;
            }

            // Send message to server
            bool send(const message<T>& msg)
            {
                if (isConnected())
                    return m_connection->send(msg);
                return false;
            }

        protected:
            // Boost ASIO context for networking operations
            boost::asio::io_context m_context;
            // Thread for running the context
            std::thread thrContext;
            // TCP socket for network communication
            boost::asio::ip::tcp::socket m_socket;

            // Smart pointer to connection object
            std::unique_ptr<connection<T>> m_connection;

        private:
            // Thread-safe queue for incoming messages
            tsQueue<owned_message<T>> m_qMessageIn;
};