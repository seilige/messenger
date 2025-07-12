#include <iostream>
#include <conio.h> // for _kbhit() and _getch()
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include "net_common.h"
#include "net_message.h"
#include "net_client.h"
#include "net_server.h"
#include <map>

using boost::asio::ip::tcp;

void DisplayMenu(bool isAuthenticated);
std::string currentInput;

// Global variables moved to private section of CustomClient class
std::map<uint32_t, std::string> m_chatHistories; // Chat histories with other users
bool m_waitingForHistory = false; // Flag indicating waiting for chat history

class CustomClient : public olc::net::client_interface<CustomMsgTypes>
{
private:
    // Variable for storing the ID of the user who sent the last message
    uint32_t m_lastMessageSender = 0;
    std::vector<ClientInfo> m_knownClients;

    bool m_isAuthenticated = false;  // Authentication flag
    std::string m_username = "";     // Username
    
    // Variables moved to private section of CustomClient class
    uint32_t m_activeChat = 0;    // ID of the user with whom we are currently chatting (0 - no active chat)
    bool m_inChatMode = false;    // Flag indicating chat mode
    
    // Variables added to private section of CustomClient class
    uint32_t m_pendingChatRequest = 0; // ID of the user who sent a chat request (0 - no pending request)
    bool m_hasChatRequest = false;     // Flag indicating presence of chat request
    
    // Variables for managing global chat functionality
    bool m_inGlobalChatMode = false;      // Flag indicating global chat mode
    bool m_waitingForGlobalHistory = false;  // Flag indicating waiting for global chat history (for this one)
    std::string m_globalChatHistory;         // Storage for global chat history (for this one)
    bool m_chatHistoryDisplayed = false;

private:
    // Method for displaying global chat messages
    void DisplayGlobalMessage(uint32_t senderUserID, const std::string& message) {
        // If we are in global chat mode, display the message appropriately
        if (m_inGlobalChatMode) {
            std::cout << "\r                                                \r"; // Clear current line

            // Check if this is our own message
            if (senderUserID == m_myID) {
                std::cout << "[You]: " << message << std::endl;
            }
            else {
                std::cout << "[User #" << senderUserID << "]: " << message << std::endl;
            }

            std::cout << "> " << currentInput; // Restore input prompt
            std::cout.flush();
        }
        // If we are in private chat mode, show as notification
        else if (m_inChatMode) {
            std::cout << "\r                                                \r"; // Clear current line
            std::cout << "\n[GLOBAL CHAT] User #" << senderUserID << ": " << message << std::endl;
            std::cout << "> " << currentInput; // Restore input prompt
            std::cout.flush();
        }
        else {
            // Display global message notification in main menu
            std::cout << "\n+---------------------------------------+" << std::endl;
            std::cout << "|           GLOBAL MESSAGE             |" << std::endl;
            std::cout << "+---------------------------------------+" << std::endl;
            std::cout << "| From: User #" << std::setw(23) << std::left << senderUserID << " |" << std::endl;
            std::cout << "| Message: " << std::setw(27) << std::left
                << (message.length() > 27 ? message.substr(0, 24) + "..." : message) << " |" << std::endl;

            // If message is too long, display full message
            if (message.length() > 27) {
                std::cout << "| Full message:                         |" << std::endl;
                std::cout << "| " << std::setw(37) << std::left << message << " |" << std::endl;
            }

            std::cout << "+---------------------------------------+" << std::endl;
            std::cout << "Press 'G' to join global chat" << std::endl;
        }
    }

public:
    // Method for entering global chat mode
    void StartGlobalChat() {
        if (!isAuthenticated()) {
            std::cout << "You must be logged in to join global chat" << std::endl;
            return;
        }

        if (m_inChatMode) {
            std::cout << "You are already in private chat mode. Exit private chat first with '/exit'" << std::endl;
            return;
        }

        m_inGlobalChatMode = true;

        std::cout << "\n=======================================" << std::endl;
        std::cout << "         GLOBAL CHAT STARTED          " << std::endl;
        std::cout << "=======================================" << std::endl;
        std::cout << "Type your messages and press Enter to send to everyone." << std::endl;
        std::cout << "Type '/exit' to leave global chat." << std::endl;
        std::cout << "Type '/history' to view chat history." << std::endl;

        // Automatically request global chat history on entry
        RequestGlobalChatHistory();

        std::cout << "\n> ";
        currentInput = "";
    }

    // Method for exiting global chat mode
    void EndGlobalChat() {
        if (m_inGlobalChatMode) {
            std::cout << "\n=======================================" << std::endl;
            std::cout << "        LEFT GLOBAL CHAT               " << std::endl;
            std::cout << "=======================================" << std::endl;
            m_inGlobalChatMode = false;
            DisplayMenu(isAuthenticated());
        }
    }

    // Check if currently in global chat mode
    bool isInGlobalChatMode() const {
        return m_inGlobalChatMode;
    }

    // Send message to global chat (from chat mode)
    bool SendGlobalChatMessage(const std::string& text) {
        if (!m_inGlobalChatMode) {
            std::cout << "Error: you are not in global chat mode" << std::endl;
            return false;
        }

        return SendGlobalMessage(text); // Use existing method
    }

public:
    // Method for sending global messages
    bool SendGlobalMessage(const std::string& text) {
        if (!isConnected()) {
            std::cout << "Error: not connected to server" << std::endl;
            return false;
        }

        if (!isAuthenticated()) {
            std::cout << "Error: you must be logged in to send global messages" << std::endl;
            return false;
        }

        if (text.empty()) {
            std::cout << "Error: cannot send empty message" << std::endl;
            return false;
        }

        if (text.size() > MAX_MESSAGE_SIZE) {
            std::cout << "Error: message too large! Maximum size is " << MAX_MESSAGE_SIZE << " characters" << std::endl;
            return false;
        }

        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::GlobalMessage;

        // Add message text size
        uint32_t textSize = static_cast<uint32_t>(text.size());
        msg << textSize;

        // Add message text character by character
        for (const char& c : text) {
            msg << c;
        }

        std::cout << "Sending global message: " << text << std::endl;
        return send(msg);
    }

    // Method for requesting global chat history
    bool RequestGlobalChatHistory() {
        if (!isConnected()) {
            std::cout << "Error: not connected to server" << std::endl;
            return false;
        }

        if (!isAuthenticated()) {
            std::cout << "Error: you must be logged in to request global chat history" << std::endl;
            return false;
        }

        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::GlobalChatHistoryRequest;

        m_waitingForGlobalHistory = true;
        std::cout << "Requesting global chat history..." << std::endl;

        return send(msg);
    }

    // Interface for sending global messages
    void SendGlobalMessageInterface() {
        if (!isAuthenticated()) {
            std::cout << "You must be logged in to send global messages" << std::endl;
            return;
        }

        std::string message;
        std::cout << "Enter global message: ";
        std::getline(std::cin, message);

        if (message.empty()) {
            std::cout << "Message cannot be empty" << std::endl;
            return;
        }

        if (SendGlobalMessage(message)) {
            std::cout << "Global message sent successfully!" << std::endl;
        }
        else {
            std::cout << "Failed to send global message" << std::endl;
        }
    }

    // Method for displaying global chat history
    void DisplayGlobalChatHistory(const std::string& history) {
        std::cout << "\n=========================================" << std::endl;
        std::cout << "           GLOBAL CHAT HISTORY          " << std::endl;
        std::cout << "=========================================" << std::endl;

        if (history.empty()) {
            std::cout << "No global chat history available" << std::endl;
        }
        else {
            std::cout << history << std::endl;
        }

        std::cout << "=========================================" << std::endl;
    }

public:
    // Method for requesting chat history with a specific user
    bool RequestChatHistory(uint32_t otherUserID) {
        if (!isConnected()) {
            std::cout << "Error: not connected to server" << std::endl;
            return false;
        }

        if (!isAuthenticated()) {
            std::cout << "Error: not authenticated" << std::endl;
            return false;
        }

        if (otherUserID == m_myID) {
            std::cout << "Error: cannot request chat history with yourself" << std::endl;
            return false;
        }

        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::ChatHistoryRequest;
        msg << otherUserID;

        m_waitingForHistory = true;
        std::cout << "Requesting chat history with user #" << otherUserID << "..." << std::endl;

        return send(msg);
    }

    // Method for displaying chat history
    void DisplayChatHistory(uint32_t otherUserID, const std::string& historyJson) {
        std::cout << "\n=========================================" << std::endl;
        std::cout << "CHAT HISTORY WITH USER #" << otherUserID << std::endl;
        std::cout << "=========================================" << std::endl;

        if (historyJson.empty() || historyJson == "{}") {
            std::cout << "No chat history found." << std::endl;
        }
        else {
            // Simple JSON display (can be improved later)
            std::cout << historyJson << std::endl;
        }

        std::cout << "=========================================" << std::endl;
    }

    // Interface for requesting chat history
    void RequestChatHistoryInterface() {
        if (!isAuthenticated()) {
            std::cout << "You must be logged in to request chat history" << std::endl;
            return;
        }

        if (m_knownClients.empty()) {
            std::cout << "No known clients. Requesting client list first..." << std::endl;
            RequestClientList();
            return;
        }

        // Display available clients
        std::cout << "\nAvailable clients:" << std::endl;
        for (const auto& client : m_knownClients) {
            if (client.id != m_myID) {
                std::cout << "  #" << client.id << " - " << client.status << std::endl;
            }
        }

        // Request user ID input
        std::string idStr;
        std::cout << "\nEnter client ID to view chat history: ";
        std::getline(std::cin, idStr);

        try {
            uint32_t id = std::stoul(idStr);
            RequestChatHistory(id);
        }
        catch (const std::exception& e) {
            std::cout << "Invalid ID format: " << idStr << std::endl;
        }
    }

    // Method for sending a chat request
    bool SendChatRequest(uint32_t clientID) {
        if (!isConnected()) {
            std::cout << "Error: no connection to server" << std::endl;
            return false;
        }

        if (clientID == m_myID) {
            std::cout << "Error: cannot send chat request to yourself" << std::endl;
            return false;
        }

        if (m_inChatMode) {
            std::cout << "Error: you are already in chat mode with client #" << m_activeChat << std::endl;
            return false;
        }

        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::ChatRequest;

        msg << clientID;

        std::cout << "Sending chat request to client #" << clientID << std::endl;
        return send(msg);
    }

    // Method for responding to a chat request
    bool SendChatResponse(uint32_t clientID, bool accepted) {
        if (!isConnected()) {
            std::cout << "Error: no connection to server" << std::endl;
            return false;
        }

        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::ChatResponse;
        msg << clientID; // Recipient ID
        msg << accepted; // Whether the request is accepted

        std::cout << "Sending chat request response to client #" << clientID << std::endl;
        return send(msg);
    }

    // Getter for checking if there's a chat request
    bool hasChatRequest() const {
        return m_hasChatRequest;
    }

    // Getter for the ID of the user who requested the chat
    uint32_t getPendingChatRequestID() const {
        return m_pendingChatRequest;
    }

    // Getter for the ID of the user who sent the last message
    uint32_t getLastMessageSender() const {
        return m_lastMessageSender;
    }

    // Method to check if we are in chat mode
    bool isInChatMode() const {
        return m_inChatMode;
    }

    // Method to get the ID of active chat partner
    uint32_t getActiveChatPartner() const {
        return m_activeChat;
    }

    // Method for sending direct messages to a specific client
    bool SendDirectMessage(uint32_t clientID, const std::string& text)
    {
        if (!isConnected()) {
            std::cout << "Error: Not connected to server" << std::endl;
            return false;
        }

        if (text.empty()) {
            std::cout << "Error: Cannot send empty message!" << std::endl;
            return false;
        }

        if (text.size() > MAX_MESSAGE_SIZE) {
            std::cout << "Error: Message too large! Maximum size is " << MAX_MESSAGE_SIZE << " characters." << std::endl;
            return false;
        }


// Check if trying to send message to self
        if (clientID == m_myID) {
            std::cout << "Error: Cannot send message to yourself!" << std::endl;
            return false;
        }

        // Check if client exists in known clients list
        bool clientExists = false;
        for (const auto& client : m_knownClients) {
            if (client.id == clientID) {
                clientExists = true;
                break;
            }
        }

        // Warn if client not found in known list
        if (!clientExists) {
            std::cout << "Warning: Client #" << clientID << " not in your known client list." << std::endl;
        }

        // Create message for direct messaging
        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::DirectMessage;

        // First write recipient ID
        msg << clientID;

        // Then write message size
        uint32_t textSize = static_cast<uint32_t>(text.size());
        msg << textSize;

        // Add each character of the string to the message
        for (const char& c : text)
        {
            msg << c;
        }

        std::cout << "Sending direct message to client #" << clientID << ": " << text << std::endl;
        return send(msg);
    }

    // Start the chat interface for selecting a client to chat with
    void StartChatInterface() {
        // Check if user is authenticated
        if (!isAuthenticated()) {
            std::cout << "You must be authorized before starting a chat" << std::endl;
            return;
        }
        
        // Check if already in chat mode
        if (m_inChatMode) {
            std::cout << "You are already in chat mode with client #" << m_activeChat << std::endl;
            std::cout << "Enter '/exit' to end the current chat" << std::endl;
            return;
        }
        
        // Check if client list is available
        if (m_knownClients.empty()) {
            RequestClientList();
            std::cout << "Please try again after receiving the client list." << std::endl;
            return;
        }
        
        // Display available clients
        std::cout << "\nAvailable clients:" << std::endl;
        for (const auto& client : m_knownClients) {
            std::string status = client.status;
            if (client.id == m_myID) {
                status += " (YOU)";
            }
            std::cout << "  #" << client.id << " - " << status << std::endl;
        }
        
        // Request client ID for chat
        std::string idStr;
        std::cout << "\nEnter ID of the client you want to chat with: ";
        std::getline(std::cin, idStr);
        
        try {
            uint32_t id = std::stoul(idStr);
            
            // Check if trying to chat with self
            if (id == m_myID) {
                std::cout << "Error: cannot start a chat with yourself" << std::endl;
                return;
            }
            
            // Check if such a client exists
            bool clientExists = false;
            for (const auto& client : m_knownClients) {
                if (client.id == id && client.id != m_myID) {
                    clientExists = true;
                    break;
                }
            }
            
            if (!clientExists) {
                std::cout << "Warning: Client #" << id << " not found in your list or it's your own ID." << std::endl;
            }
            
            // Send a chat request instead of starting the chat directly
            if (SendChatRequest(id)) {
                std::cout << "Chat request sent to client #" << id << ". Waiting for response..." << std::endl;
            }
            else {
                std::cout << "Error sending chat request" << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cout << "Invalid ID format: " << idStr << std::endl;
        }
    }

    // Getter for client's personal ID
    uint32_t getMyID() const {
        return m_myID;
    }

    // Improved interface for sending private messages to multiple recipients
    void SendPrivateMessageInterface()
    {
        // Check if client list is available
        if (m_knownClients.empty()) {
            std::cout << "No known clients. Requesting client list first..." << std::endl;
            RequestClientList();
            return;
        }

        // Show available clients
        std::cout << "\nAvailable clients:" << std::endl;
        for (const auto& client : m_knownClients) {
            std::string status = client.status;
            if (client.id == m_myID) {
                status += " (YOU)";
            }
            std::cout << "  #" << client.id << " - " << status << std::endl;
        }

        // Request recipient list from user with commas
        std::string recipients;
        std::cout << "\nEnter client ID(s) to message (separate multiple IDs with commas): ";
        std::getline(std::cin, recipients);

        // Parse recipient list
        std::vector<uint32_t> recipientIDs;
        std::stringstream ss(recipients);
        std::string idStr;

        while (std::getline(ss, idStr, ',')) {
            // Remove spaces before and after the number
            idStr.erase(0, idStr.find_first_not_of(" \t"));
            idStr.erase(idStr.find_last_not_of(" \t") + 1);

            try {
                uint32_t id = std::stoul(idStr);
                recipientIDs.push_back(id);
            }
            catch (const std::exception& e) {
                std::cout << "Invalid ID format: " << idStr << std::endl;
            }
        }

        // Validate recipient list
        if (recipientIDs.empty()) {
            std::cout << "No valid recipient IDs provided." << std::endl;
            return;
        }

        // Request message
        std::string message;
        std::cout << "Enter message: ";
        std::getline(std::cin, message);

        if (message.empty()) {
            std::cout << "Message cannot be empty." << std::endl;
            return;
        }

        // Check message size limit
        if (message.size() > MAX_MESSAGE_SIZE) {
            std::cout << "Error: Message too large! Maximum size is " << MAX_MESSAGE_SIZE << " characters." << std::endl;
            return;
        }

        // Send message to each recipient
        for (const auto& id : recipientIDs) {
            if (SendDirectMessage(id, message)) {
                std::cout << "Message sent to client #" << id << std::endl;
            }
            else {
                std::cout << "Failed to send message to client #" << id << std::endl;
            }
        }
    }

    // Quick reply method for responding to incoming messages
    void QuickReplyToMessage(uint32_t senderID) {
        std::cout << "\nQuick reply to client #" << senderID << std::endl;
        std::cout << "Enter your message (or press Enter to cancel): ";

        std::string replyMessage;
        std::getline(std::cin, replyMessage);

        if (!replyMessage.empty()) {
            if (SendDirectMessage(senderID, replyMessage)) {
                std::cout << "Reply sent successfully!" << std::endl;
            }
            else {
                std::cout << "Failed to send reply!" << std::endl;
            }
        }
        else {
            std::cout << "Reply cancelled." << std::endl;
        }
    }

    // Check if client is authenticated
    bool isAuthenticated() const {
        return m_isAuthenticated;
    }

    // Send a message in active chat mode
    bool SendChatMessage(const std::string& text) {
        // Check if in chat mode
        if (!m_inChatMode || m_activeChat == 0) {
            std::cout << "Error: you are not in chat mode" << std::endl;
            return false;
        }

        // Check connection status
        if (!isConnected()) {
            std::cout << "Error: no connection to server" << std::endl;
            return false;
        }

        // Validate message content
        if (text.empty()) {
            std::cout << "Error: Cannot send empty message!" << std::endl;
            return false;
        }

        // Check message size limit
        if (text.size() > MAX_MESSAGE_SIZE) {
            std::cout << "Error: Message too large! Maximum size is " << MAX_MESSAGE_SIZE << " characters." << std::endl;
            return false;
        }

        // Create message for chat
        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::DirectMessage;

        // Add recipient ID
        msg << m_activeChat;

        // Add message size
        uint32_t textSize = static_cast<uint32_t>(text.size());
        msg << textSize;

        // Add message content character by character
        for (const char& c : text) {
            msg << c;
        }

        return send(msg);
    }
    
    // Start a chat session with specified client
    void StartChat(uint32_t clientID) {
        // Check connection status
        if (!isConnected()) {
            std::cout << "Error: no connection to server" << std::endl;
            return;
        }
        
        // Check if trying to chat with self
        if (clientID == m_myID) {
            std::cout << "Error: cannot start chat with yourself" << std::endl;
            return;
        }

        // Check if client exists in known clients list
        bool clientExists = false;
        for (const auto& client : m_knownClients) {
            if (client.id == clientID) {
                clientExists = true;
                break;
            }
        }
        
        if (!clientExists) {
            std::cout << "Warning: Client #" << clientID << " not found in your list." << std::endl;
            std::cout << "It is recommended to update the client list using the 'L' command" << std::endl;
        }

        // Set chat mode variables
        m_activeChat = clientID;
        m_inChatMode = true;
        m_chatHistoryDisplayed = false; // Reset flag for new chat session

        // Load chat history for this conversation
        std::cout << "Loading chat history..." << std::endl;
        RequestChatHistory(clientID);
    }

    // Method to exit chat mode
    void EndChat() {
        if (m_inChatMode) {
            std::cout << "\n=======================================" << std::endl;
            std::cout << "CHAT ENDED WITH CLIENT #" << m_activeChat << std::endl;
            std::cout << "=======================================" << std::endl;
            
            // Reset chat mode variables
            m_activeChat = 0;
            m_inChatMode = false;
            m_chatHistoryDisplayed = false; // Reset flag when exiting chat
            
            // Show menu after exiting chat
            DisplayMenu(isAuthenticated());
        }
    }

    // Method for accepting a chat request
    void AcceptChatRequest() {
        // Check if there's an active chat request
        if (!m_hasChatRequest || m_pendingChatRequest == 0) {
            std::cout << "No active chat requests" << std::endl;
            return;
        }

        // Send acceptance response and start chat
        if (SendChatResponse(m_pendingChatRequest, true)) {
            std::cout << "Chat request accepted" << std::endl;
            StartChat(m_pendingChatRequest);
            
            // Clear pending request
            m_hasChatRequest = false;
            m_pendingChatRequest = 0;
        }
    }

    // Method for declining a chat request
    void DeclineChatRequest() {
        // Check if there's an active chat request
        if (!m_hasChatRequest || m_pendingChatRequest == 0) {
            std::cout << "No active chat requests" << std::endl;
            return;
        }

        // Send decline response
        if (SendChatResponse(m_pendingChatRequest, false)) {
            std::cout << "Chat request declined" << std::endl;
            
            // Clear pending request
            m_hasChatRequest = false;
            m_pendingChatRequest = 0;
        }
    }

    // Handle incoming chat request from another client
    void HandleIncomingChatRequest(uint32_t senderID) {
        // Check if request is from own ID (should not happen)
        if (senderID == m_myID) {
            std::cout << "Warning: Received chat request from your own ID. This is unusual." << std::endl;
            // Automatically decline request from own ID
            SendChatResponse(senderID, false);
            return;
        }

        // Check if already in chat mode
        if (m_inChatMode) {
            std::cout << "\nYou already have a chat room open with the client #" << m_activeChat << std::endl;
            std::cout << "First, close the current chat with '/exit'" << std::endl;
            
            // Automatically decline request if already in chat
            SendChatResponse(senderID, false);
            std::cout << "A chat request from client #" << senderID << " automatically rejected." << std::endl;
            return;
        }

        // Store pending request details
        m_pendingChatRequest = senderID;
        m_hasChatRequest = true;

        // Display chat request notification with visual formatting
        std::cout << "\n???????????????????????????????????????????" << std::endl;
        std::cout << "?       INCOMING CHAT REQUEST              ?" << std::endl;
        std::cout << "? From client #" << std::setw(23) << std::left << senderID << " ?" << std::endl;
        std::cout << "???????????????????????????????????????????" << std::endl;
        std::cout << "? Press 'Y' to accept or 'N' to            ?" << std::endl;
        std::cout << "? decline the request.                     ?" << std::endl;
        std::cout << "???????????????????????????????????????????" << std::endl;

        // Flush output buffer to ensure immediate display
        std::cout.flush();

        // Sound notification (if supported by terminal)
        std::cout << '\a';
    }

    // Processing incoming messages from the server
    void ProcessMessages()
    {
        if (!incoming().empty())
        {
            // Get message from queue
            olc::net::owned_message<CustomMsgTypes> owned_msg = incoming().front();
            // Remove it from queue
            incoming().pop_front();

            // Reset read position before processing
            owned_msg.msg.reset_read_position();

            switch (owned_msg.msg.header.id)
            {
                // Handle global messages
                case CustomMsgTypes::GlobalMessage:
                {
                    std::cout << "[CLIENT] Received global message" << std::endl;

                    // Read sender user ID
                    uint32_t senderUserID = 0;
                    owned_msg.msg >> senderUserID;

                    // Read message size
                    uint32_t messageSize = 0;
                    owned_msg.msg >> messageSize;

                    // Validate message size
                    if (messageSize > MAX_MESSAGE_SIZE) {
                        std::cerr << "Global message too large: " << messageSize << std::endl;
                        break;
                    }
                    // Read the size of the message text
                std::string messageText;
                messageText.reserve(messageSize);

                for (uint32_t i = 0; i < messageSize; i++) {
                    char c;
                    owned_msg.msg >> c;
                    messageText.push_back(c);
                }

                // Display the global message with sender information
                DisplayGlobalMessage(senderUserID, messageText);

                break;
            }

            case CustomMsgTypes::GlobalChatHistoryResponse:
            {
                std::cout << "[CLIENT] Received global chat history response" << std::endl;

                // Read the size of the history data
                uint32_t historySize = 0;
                owned_msg.msg >> historySize;

                // Validate the history size to prevent buffer overflow
                if (historySize > 50000) { // Reasonable limit for history
                    std::cerr << "Global chat history too large: " << historySize << std::endl;
                    break;
                }

                // Read the chat history character by character
                std::string chatHistory;
                chatHistory.reserve(historySize);

                for (uint32_t i = 0; i < historySize; i++) {
                    char c;
                    owned_msg.msg >> c;
                    chatHistory.push_back(c);
                }

                // Store the received history
                m_globalChatHistory = chatHistory;
                m_waitingForGlobalHistory = false;

                // If we're currently in global chat mode, display the history immediately
                if (m_inGlobalChatMode) {
                    std::cout << "\r                                                \r"; // Clear current line
                    std::cout << "\n--- Chat History ---" << std::endl;
                    if (!chatHistory.empty()) {
                        std::cout << chatHistory << std::endl;
                    }
                    else {
                        std::cout << "No previous messages" << std::endl;
                    }
                    std::cout << "--- End History ---" << std::endl;
                    std::cout << "> " << currentInput;
                    std::cout.flush();
                }
                else {
                    DisplayGlobalChatHistory(chatHistory);
                    DisplayMenu(isAuthenticated());
                }

                break;
            }
            case CustomMsgTypes::ChatHistoryResponse:
            {
                std::cout << "[CLIENT] Received chat history response" << std::endl;

                // Read the ID of the user whose chat history we're receiving
                uint32_t otherUserID = 0;
                owned_msg.msg >> otherUserID;

                // Read the size of the history data
                uint32_t historySize = 0;
                owned_msg.msg >> historySize;

                // Validate the history size to prevent buffer overflow
                if (historySize > MAX_MESSAGE_SIZE) {
                    std::cerr << "Chat history too large: " << historySize << std::endl;
                    break;
                }

                // Read the private chat history character by character
                std::string chatHistory;
                chatHistory.reserve(historySize);

                for (uint32_t i = 0; i < historySize; i++) {
                    char c;
                    owned_msg.msg >> c;
                    chatHistory.push_back(c);
                }

                // Store the received history for this user
                m_chatHistories[otherUserID] = chatHistory;
                m_waitingForHistory = false;

                // Display history only if we're in chat mode with this user and haven't shown it yet
                if (isInChatMode() && m_activeChat == otherUserID && !m_chatHistoryDisplayed) {
                    DisplayChatHistory(otherUserID, chatHistory);
                    m_chatHistoryDisplayed = true; // Set flag to prevent multiple displays
                }
                // If we're not in chat mode, display the history and show menu
                else if (!isInChatMode()) {
                    DisplayChatHistory(otherUserID, chatHistory);
                    DisplayMenu(isAuthenticated());
                }

                break;
            }



            case CustomMsgTypes::ChatRequest:
            {
                // Extract the ID of the user requesting to chat
                uint32_t senderID = 0;
                owned_msg.msg >> senderID;

                // Handle the incoming chat request from this user
                HandleIncomingChatRequest(senderID);
                break;
            }

            // Handle client information response in the ProcessMessages() method of CustomClient
            case CustomMsgTypes::ClientInfoResponse:
            {
                // Extract the client ID
                uint32_t clientID = 0;
                owned_msg.msg >> clientID;

                // Extract the size of the username
                uint32_t usernameSize = 0;
                owned_msg.msg >> usernameSize;

                // Validate username size to prevent buffer overflow
                if (usernameSize > MAX_MESSAGE_SIZE) {
                    std::cerr << "Incorrect size of username" << std::endl;
                    break;
                }

                // Read the username character by character
                std::string username;
                username.reserve(usernameSize);
                for (uint32_t i = 0; i < usernameSize; i++) {
                    char c;
                    owned_msg.msg >> c;
                    username.push_back(c);
                }

                // Extract the size of the status
                uint32_t statusSize = 0;
                owned_msg.msg >> statusSize;

                // Validate status size to prevent buffer overflow
                if (statusSize > MAX_MESSAGE_SIZE) {
                    std::cerr << "Incorrect size of status" << std::endl;
                    break;
                }

                // Read the status character by character
                std::string status;
                status.reserve(statusSize);
                for (uint32_t i = 0; i < statusSize; i++) {
                    char c;
                    owned_msg.msg >> c;
                    status.push_back(c);
                }

                // Display the client information
                DisplayClientInfo(clientID, username, status);

                // Update our own information if this is our client ID
                if (clientID == m_myID) {
                    m_username = username;
                    // If the ID doesn't match our stored ID, update it
                    if (clientID != m_myID) {
                        std::cout << "Updating your client ID from #" << m_myID << " to #" << clientID << std::endl;
                        m_myID = clientID;
                    }
                }

                // Update the client information in our known clients list
                bool clientFound = false;
                for (auto& client : m_knownClients) {
                    if (client.id == clientID) {
                        client.status = status;
                        client.lastSeen = std::chrono::system_clock::now();
                        clientFound = true;
                        break;
                    }
                }

                // If client is not in our list, add them
                if (!clientFound) {
                    m_knownClients.push_back({ clientID, status, std::chrono::system_clock::now() });
                }

                break;
            }

            case CustomMsgTypes::ChatResponse:
            {
                // Get the sender ID of the response
                uint32_t senderID = 0;
                owned_msg.msg >> senderID;
                // Get the response result (accepted/declined)
                bool accepted = false;
                owned_msg.msg >> accepted;

                // Validate that this isn't a response from our own ID
                if (senderID == m_myID) {
                    std::cout << "Warning: Received chat response from your own ID. This is unusual." << std::endl;
                    break;
                }

                if (accepted) {
                    std::cout << "Client #" << senderID << " accepted your chat request!" << std::endl;
                    // Start the chat session with this client
                    StartChat(senderID);
                    std::cout << "\n=======================================" << std::endl;
                    std::cout << "CHAT STARTED WITH CLIENT #" << senderID << std::endl;
                    std::cout << "=======================================" << std::endl;
                    std::cout << "Type your messages and press Enter to send." << std::endl;
                    std::cout << "Type '/exit' to end the chat." << std::endl;
                    std::cout << "\n> ";
                    currentInput = "";
                }
                else {
                    std::cout << "Client #" << senderID << " declined your chat request." << std::endl;
                    DisplayMenu(isAuthenticated());
                }
                // Update the client's status in the list
                for (auto& client : m_knownClients) {
                    if (client.id == senderID) {
                        client.lastSeen = std::chrono::system_clock::now();
                        client.status = accepted ? "Online (chatting with you)" : "Online";
                        break;
                    }
                }
                break;
            }

            // Handle server acceptance response in the ProcessMessages() method of CustomClient
            case CustomMsgTypes::ServerAccept:
            {
                // When server accepts our connection, it should send us our ID
                if (owned_msg.msg.body.size() >= sizeof(uint32_t)) {
                    uint32_t oldID = m_myID; // Store previous ID for comparison
                    owned_msg.msg >> m_myID;
                    std::cout << "Server accepted connection! Your client ID is #" << m_myID << std::endl;
                    if (oldID != 0 && oldID != m_myID) {
                        std::cout << "WARNING: Your ID changed from #" << oldID << " to #" << m_myID << std::endl;
                    }
                }
                else {
                    std::cout << "Server accepted connection!" << std::endl;
                }
                // Request client list on connection
                RequestClientList();
                // Request our own client information from the server
                RequestClientInfo();
                break;
            }

            case CustomMsgTypes::ServerDeny:
                std::cout << "Server denied connection!" << std::endl;
                break;

            case CustomMsgTypes::RegisterResponse:
            {
                // Get success flag
                bool success = false;
                owned_msg.msg >> success;

                // Read message size
                uint32_t messageSize = 0;
                owned_msg.msg >> messageSize;

                // Security check to prevent buffer overflow
                if (messageSize > MAX_MESSAGE_SIZE) {
                    std::cerr << "Incorrect size" << std::endl;
                    break;
                }

                // Read message character by character
                std::string message;
                message.reserve(messageSize);

                for (uint32_t i = 0; i < messageSize; i++)
                {
                    char c;
                    owned_msg.msg >> c;
                    message.push_back(c);
                }

                // Display registration result in a formatted box
                std::cout << "?????????????????????????????????????????" << std::endl;
                std::cout << "?     Result of registration            ?" << std::endl;
                if (success) {
                    m_isAuthenticated = true;
                    std::cout << "? Status: Success                      ?" << std::endl;
                }
                else {
                    std::cout << "? Status: Failed                       ?" << std::endl;
                }

                // Split message into lines for better display formatting
                size_t pos = 0;
                size_t lineLength = 36; // Maximum line length for the box
                while (pos < message.length()) {
                    std::string line = message.substr(pos, std::min(lineLength, message.length() - pos));
                    pos += lineLength;
                    std::cout << "? " << std::setw(36) << std::left << line << " ?" << std::endl;
                }
                std::cout << "?????????????????????????????????????????" << std::endl;

                break;
            }

            case CustomMsgTypes::ServerMessage:
            {
                // Read message size
                uint32_t messageSize = 0;
                owned_msg.msg >> messageSize;

                // Check size correctness to prevent buffer overflow
                if (messageSize > MAX_MESSAGE_SIZE) {
                    std::cerr << "Server message too large: " << messageSize << std::endl;
                    break;
                }

                // Security check: ensure message size doesn't exceed body size
                if (messageSize > owned_msg.msg.body.size()) {
                    std::cerr << "Invalid message size: declared " << messageSize
                        << " but body only has " << owned_msg.msg.body.size() << " bytes" << std::endl;
                    break;
                }

                // Read message character by character
                std::string message;
                message.reserve(messageSize); // Memory allocation optimization

                for (uint32_t i = 0; i < messageSize; i++)
                {
                    char c;
                    owned_msg.msg >> c;
                    message.push_back(c);
                }

                // Check if message is client list and display accordingly
                if (message.find("Connected clients:") != std::string::npos) {
                    DisplayConnectedClients(message, m_knownClients, m_myID);

                }
                else {
                    std::cout << "Message from server: " << message << std::endl;
                }
                break;
            }
           // Add processing for LoginResponse to ProcessMessages() method in CustomClient class:
            case CustomMsgTypes::LoginResponse:
            {
                // Extract success flag from the message
                bool success = false;
                owned_msg.msg >> success;

                // Read the size of the response message
                uint32_t messageSize = 0;
                owned_msg.msg >> messageSize;

                // Security check: validate message size to prevent buffer overflow
                if (messageSize > MAX_MESSAGE_SIZE) {
                    std::cerr << "Incorrect message size" << std::endl;
                    break;
                }

                // Read the message content character by character
                std::string message;
                message.reserve(messageSize);

                for (uint32_t i = 0; i < messageSize; i++)
                {
                    char c;
                    owned_msg.msg >> c;
                    message.push_back(c);
                }

                // Update client authentication status based on server response
                m_isAuthenticated = success;

                // Display formatted login result to user
                std::cout << "╔═════════════════════════════════════╗" << std::endl;
                std::cout << "║     Result of system login            ║" << std::endl;
                if (success) {
                    std::cout << "║ Status: Success                      ║" << std::endl;
                }
                else {
                    std::cout << "║ Status: Failed                       ║" << std::endl;
                }

                // Split message into lines for better display formatting
                size_t pos = 0;
                size_t lineLength = 36; // Maximum line length for display box
                while (pos < message.length()) {
                    std::string line = message.substr(pos, std::min(lineLength, message.length() - pos));
                    pos += lineLength;
                    std::cout << "║ " << std::setw(36) << std::left << line << " ║" << std::endl;
                }
                std::cout << "╚═════════════════════════════════════╝" << std::endl;

                break;
            }

            // Handle direct (private) messages between clients
            case CustomMsgTypes::DirectMessage:
            {
                // Extract sender ID from the message
                uint32_t senderID = 0;
                owned_msg.msg >> senderID;

                // Read message size
                uint32_t messageSize = 0;
                owned_msg.msg >> messageSize;

                // Security validation: check message size bounds
                if (messageSize > MAX_MESSAGE_SIZE || messageSize > owned_msg.msg.body.size()) {
                    std::cerr << "Incorrect size of private message" << std::endl;
                    break;
                }

                // Read message content character by character
                std::string message;
                message.reserve(messageSize);

                for (uint32_t i = 0; i < messageSize; i++)
                {
                    char c;
                    owned_msg.msg >> c;
                    message.push_back(c);
                }

                // Store sender ID for potential reply functionality
                m_lastMessageSender = senderID;

                // If we're in chat mode with this specific user, display message inline
                if (m_inChatMode && senderID == m_activeChat) {
                    std::cout << "\r                                                \r"; // Clear current input line
                    std::cout << "[Client #" << senderID << "]: " << message << std::endl;
                    std::cout << "> " << currentInput; // Restore current input prompt
                    std::cout.flush();
                }
                else {
                    // Display message in notification format
                    DisplayPrivateMessage(senderID, message);
                }

                // Update or add sender to known clients list
                bool found = false;
                for (auto& client : m_knownClients) {
                    if (client.id == senderID) {
                        client.lastSeen = std::chrono::system_clock::now();
                        if (!m_inChatMode || senderID != m_activeChat) {
                            client.status = "Online (sent you message)";
                        }
                        found = true;
                        break;
                    }
                }

                // If sender is not in known clients list, add them
                if (!found) {
                    m_knownClients.push_back({ senderID, "Online (sent you message)", std::chrono::system_clock::now() });
                }

                break;
            }

            default:
                std::cout << "Unknown message type: " << static_cast<int>(owned_msg.msg.header.id) << std::endl;
                break;
            }
        }
    }
};

// Main application entry point
int main()
{
    CustomClient c;

    // Attempt to connect to the server
    if (c.Connect("127.0.0.1", 60000))
    {
        std::cout << "Connected to server!" << std::endl;
    }
    else
    {
        std::cout << "Failed to connect to server!" << std::endl;
        return -1;
    }

    bool bQuit = false;

    // Display initial menu based on authentication status
    DisplayMenu(c.isAuthenticated());

    // Main application loop
    while (!bQuit)
    {
        // Verify connection to server is still active
        if (c.isConnected())
        {
            // Process incoming messages from server
            c.ProcessMessages();

            // Check for keyboard input
            if (_kbhit())
            {
                int key = _getch();

                // Handle input differently based on current mode (chat vs menu)
                if (c.isInChatMode() || c.isInGlobalChatMode())
                {
                    if (key == 13) // Enter key pressed
                    {
                        if (!currentInput.empty()) {
                            // Check for special chat commands
                            if (currentInput == "/exit") {
                                if (c.isInGlobalChatMode()) {
                                    c.EndGlobalChat();
                                }
                                else {
                                    c.EndChat();
                                }
                                currentInput = "";
                            }
                            else if (currentInput == "/history" && c.isInGlobalChatMode()) {
                                c.RequestGlobalChatHistory();
                                currentInput = "";
                                std::cout << "\n> ";
                            }
                            else {
                                // Send message based on current chat mode
                                if (c.isInGlobalChatMode()) {
                                    c.SendGlobalChatMessage(currentInput);
                                }
                                else {
                                    c.SendChatMessage(currentInput);
                                    std::cout << "\r[You]: " << currentInput << std::endl;
                                }
                                currentInput = "";
                                std::cout << "> ";
                            }
                        }
                        else {
                            std::cout << "\n> ";
                        }
                    }
                    else if (key == 8) // Backspace key
                    {
                        if (!currentInput.empty()) {
                            currentInput.pop_back();
                            std::cout << "\r> " << currentInput << " \b"; // Clear last character visually
                        }
                    }
                    else if (key >= 32 && key <= 126) // Printable characters
                    {
                        currentInput += static_cast<char>(key);
                        std::cout << static_cast<char>(key); // Echo character to console
                    }
                }
                else
                {
                    // Handle main menu navigation
                    switch (toupper(key))
                    {
                    case 'D': // Send direct message
                        c.SendPrivateMessageInterface();
                        DisplayMenu(c.isAuthenticated());
                        break;
                    case 'C': // Start private chat
                        c.StartChatInterface();
                        if (c.isInChatMode()) {
                            std::cout << "\n> "; // Show chat prompt
                            currentInput = ""; // Reset input buffer
                        }
                        break;
                    case 'L': // Login or list users (context dependent)
                        if (c.isAuthenticated()) {
                            std::cout << "Requesting user list..." << std::endl;
                            c.RequestClientList();
                        }
                        else {
                            c.LoginUserInterface();
                            DisplayMenu(c.isAuthenticated());
                        }
                        break;

                    case 'Y': // Accept chat request
                        if (c.hasChatRequest()) {
                            c.AcceptChatRequest();
                            if (c.isInChatMode()) {
                                std::cout << "\n=======================================" << std::endl;
                                std::cout << "CHAT STARTED WITH CLIENT #" << c.getActiveChatPartner() << std::endl;
                                std::cout << "=======================================" << std::endl;
                                std::cout << "Type your messages and press Enter to send." << std::endl;
                                std::cout << "Type '/exit' to end the chat." << std::endl;
                                std::cout << "\n> ";
                                currentInput = "";
                            }
                        }
                        else {
                            std::cout << "No pending chat requests." << std::endl;
                            DisplayMenu(c.isAuthenticated());
                        }
                        break;

                    case 'N': // Decline chat request
                        if (c.hasChatRequest()) {
                            c.DeclineChatRequest();
                            DisplayMenu(c.isAuthenticated());
                        }
                        else {
                            std::cout << "No pending chat requests." << std::endl;
                            DisplayMenu(c.isAuthenticated());
                        }
                        break;
                    case 'R': // Register new user
                        if (!c.isAuthenticated()) {
                            c.RegisterUserInterface(c.getMyID());
                        }
                        else {
                            std::cout << "You are already registered in the system" << std::endl;
                        }
                        DisplayMenu(c.isAuthenticated());
                        break;
                    case 'I': // Display user information
                        if (c.isAuthenticated()) {
                            std::cout << "Current user information:" << std::endl;
                            std::cout << "ID: " << c.getMyID() << std::endl;
                        }
                        else {
                            std::cout << "You are not logged into the system" << std::endl;
                        }
                        break;
                    case 'G': // Join global chat
                        if (c.isAuthenticated()) {
                            c.StartGlobalChat();
                            if (c.isInGlobalChatMode()) {
                                std::cout << "\n> "; // Show chat prompt
                                currentInput = ""; // Reset input buffer
                            }
                        }
                        else {
                            std::cout << "You must be logged in to join global chat" << std::endl;
                            DisplayMenu(c.isAuthenticated());
                        }
                        break;

                    case 'H': // Request global chat history
                        if (c.isAuthenticated()) {
                            c.RequestGlobalChatHistory();
                        }
                        else {
                            std::cout << "You must be logged in to view global chat history" << std::endl;
                            DisplayMenu(c.isAuthenticated());
                        }
                        break;

                    case 'Q': // Quit application
                        bQuit = true;
                        break;
                    }
                }
            }
            // Small delay to reduce CPU usage in the main loop
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        else
        {
            // Connection lost, exit application
            bQuit = true;
        }
    }

    return 0;
}