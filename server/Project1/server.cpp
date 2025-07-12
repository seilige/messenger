#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <map>
#include <mutex>
#include <fstream>
#include "simdjson.h"
#include "net_common.h"
#include "net_message.h"
#include "net_server.h"
#include "global_chat.h"
#include "user_manager.h"
#include "net_server_chat.h"

using boost::asio::ip::tcp;

// Custom server class that inherits from server interface, global chat manager, and server chat interface
class CustomServer : public olc::net::server_interface<CustomMsgTypes>, public GlobalChatManager, public olc::net::server_chat_interface<CustomMsgTypes>
{
public:
    // Constructor: initializes server with port and user database
    CustomServer(uint16_t nPort) : olc::net::server_interface<CustomMsgTypes>(nPort), userManager("users.json")
    {
        std::cout << "[SERVER] User database initialized\n";
    }

    // Override method called when client is validated - sends welcome message
    virtual void onClientValidated(std::shared_ptr<olc::net::connection<CustomMsgTypes>> client) override
    {
        olc::net::server_interface<CustomMsgTypes>::onClientValidated(client);

        // Send welcome message to newly connected client
        SendMessageToClient(client, "Welcome to the server! You are client #" + std::to_string(client->getID()));
    }

private:
    UserManager userManager;                                  // Manages user data and authentication
    std::map<uint32_t, std::string> authenticatedUsers;      // Maps client ID to username
    std::map<std::string, uint32_t> userToClientMap;         // Maps username to client ID
    std::mutex authMutex;                                     // Mutex for authentication operations
    std::mutex chatLogMutex;                                  // Mutex for chat log file operations

    // Formats JSON chat history into readable text format
    std::string formatChatHistory(const std::string& jsonHistory) {
        if (jsonHistory.empty()) {
            return "No messages found in chat history.";
        }

        // Check if history is already formatted to avoid double formatting
        if (jsonHistory.find("=== Chat History ===") != std::string::npos) {
            std::cout << "[DEBUG] Already formatted history received, returning as-is\n";
            return jsonHistory;
        }

        try {
            simdjson::dom::parser parser;
            simdjson::dom::element doc;

            std::cout << "[DEBUG] Raw JSON input: " << jsonHistory.substr(0, 200) << "...\n";

            // Parse the JSON string
            auto error = parser.parse(jsonHistory).get(doc);
            if (error != simdjson::SUCCESS) {
                std::cerr << "[SERVER] JSON parsing failed: " << simdjson::error_message(error) << "\n";
                return "Error: Invalid JSON format in chat history.";
            }

            std::string formattedHistory = "\n=== CHAT HISTORY ===\n";

            // Check if this is a private conversation (has conversation_id field)
            std::string_view conversationId_view;
            if (doc["conversation_id"].get(conversationId_view) == simdjson::SUCCESS) {
                // Private conversation format
                std::string conversationId(conversationId_view);
                formattedHistory += "Conversation: " + conversationId + "\n\n";

                simdjson::dom::array messages;
                if (doc["messages"].get(messages) == simdjson::SUCCESS) {
                    // Process each message in the conversation
                    for (auto message : messages) {
                        std::string_view senderUsername_view;
                        std::string_view recipientUsername_view;
                        std::string_view messageText_view;
                        std::string_view timestamp_view;

                        // Extract message fields
                        if (message["sender_username"].get(senderUsername_view) == simdjson::SUCCESS &&
                            message["recipient_username"].get(recipientUsername_view) == simdjson::SUCCESS &&
                            message["message_text"].get(messageText_view) == simdjson::SUCCESS) {

                            std::string senderUsername(senderUsername_view);
                            std::string recipientUsername(recipientUsername_view);
                            std::string messageText(messageText_view);
                            std::string timestamp = "";

                            // Extract timestamp if available
                            if (message["timestamp"].get(timestamp_view) == simdjson::SUCCESS) {
                                timestamp = std::string(timestamp_view);
                            }
                            else {
                                timestamp = "Unknown time";
                            }

                            // Format message as: [timestamp] sender -> recipient: message
                            formattedHistory += "[" + timestamp + "] " + senderUsername + " -> " + recipientUsername + ": " + messageText + "\n";
                        }
                    }
                }
                else {
                    formattedHistory += "No messages in this conversation.\n";
                }
            }
            else {
                // Global chat format (no conversation_id)
                simdjson::dom::array messages;
                if (doc["messages"].get(messages) == simdjson::SUCCESS) {
                    // Process each global message
                    for (auto message : messages) {
                        std::string_view senderUsername_view;
                        std::string_view messageText_view;
                        std::string_view timestamp_view;

                        // Extract message fields for global chat
                        if (message["sender_username"].get(senderUsername_view) == simdjson::SUCCESS &&
                            message["message_text"].get(messageText_view) == simdjson::SUCCESS) {

                            std::string senderUsername(senderUsername_view);
                            std::string messageText(messageText_view);
                            std::string timestamp = "";

                            // Try to get timestamp from different possible fields
                            if (message["timestamp"].get(timestamp_view) == simdjson::SUCCESS) {
                                timestamp = std::string(timestamp_view);
                            }
                            else if (message["created_date"].get(timestamp_view) == simdjson::SUCCESS) {
                                timestamp = std::string(timestamp_view);
                            }
                            else {
                                timestamp = "Unknown time";
                            }

                            // Format message as: [timestamp] sender: message
                            formattedHistory += "[" + timestamp + "] " + senderUsername + ": " + messageText + "\n";
                        }
                    }
                }
                else {
                    formattedHistory += "No messages found.\n";
                }
            }

            formattedHistory += "=== END OF HISTORY ===\n";
            return formattedHistory;

        }
        catch (const std::exception& e) {
            std::cerr << "[SERVER] Exception in formatChatHistory: " << e.what() << "\n";
            std::cerr << "[SERVER] JSON content (first 500 chars): " << jsonHistory.substr(0, 500) << "\n";
            return "Error: Unable to format chat history - " + std::string(e.what());
        }
    }

// Saves a chat message to the appropriate JSON file for the conversation
void saveChatMessage(const std::string& senderUsername, uint32_t senderUserID,
    const std::string& recipientUsername, uint32_t recipientUserID,
    const std::string& messageText) {
    std::lock_guard<std::mutex> lock(chatLogMutex);

    try {
        // Generate filename for the conversation between these two users
        std::string chatFileName = generateChatFileName(senderUsername, recipientUsername);

        // Create conversation ID (alphabetical order for consistency)
        std::string conversationID;
        if (senderUsername < recipientUsername) {
            conversationID = senderUsername + "_" + recipientUsername;
        }
        else {
            conversationID = recipientUsername + "_" + senderUsername;
        }

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        time_t time_now = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        struct tm timeinfo;

        // Thread-safe time formatting for different platforms
#ifdef _WIN32
        localtime_s(&timeinfo, &time_now);
#else
        localtime_r(&time_now, &timeinfo);
#endif
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

        // Generate unique message ID based on timestamp in milliseconds
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Escape special characters in message text for JSON format
        std::string escapedMessageText = messageText;
        size_t pos = 0;
        // Escape double quotes
        while ((pos = escapedMessageText.find("\"", pos)) != std::string::npos) {
            escapedMessageText.replace(pos, 1, "\\\"");
            pos += 2;
        }
        // Escape newlines
        while ((pos = escapedMessageText.find("\n", pos)) != std::string::npos) {
            escapedMessageText.replace(pos, 1, "\\n");
            pos += 2;
        }
        // Escape carriage returns
        while ((pos = escapedMessageText.find("\r", pos)) != std::string::npos) {
            escapedMessageText.replace(pos, 1, "\\r");
            pos += 2;
        }

        // Read existing file content if it exists
        std::string existingContent;
        std::ifstream inFile(chatFileName);
        bool fileExists = false;

        if (inFile.is_open()) {
            std::string line;
            while (std::getline(inFile, line)) {
                existingContent += line + "\n";
            }
            inFile.close();
            fileExists = !existingContent.empty();
        }

        // Create new message JSON object
        std::string newMessage = "    {\n";
        newMessage += "      \"message_id\": " + std::to_string(timestamp) + ",\n";
        newMessage += "      \"conversation_id\": \"" + conversationID + "\",\n";
        newMessage += "      \"sender_username\": \"" + senderUsername + "\",\n";
        newMessage += "      \"sender_user_id\": " + std::to_string(senderUserID) + ",\n";
        newMessage += "      \"recipient_username\": \"" + recipientUsername + "\",\n";
        newMessage += "      \"recipient_user_id\": " + std::to_string(recipientUserID) + ",\n";
        newMessage += "      \"message_text\": \"" + escapedMessageText + "\",\n";
        newMessage += "      \"timestamp\": \"" + std::string(timeStr) + "\",\n";
        newMessage += "      \"message_type\": \"direct_message\"\n";
        newMessage += "    }";

        // Open file for writing
        std::ofstream outFile(chatFileName);
        if (outFile.is_open()) {
            if (!fileExists) {
                // Create new conversation file with initial structure
                outFile << "{\n";
                outFile << "  \"conversation_id\": \"" + conversationID + "\",\n";
                outFile << "  \"participants\": [\"" + senderUsername + "\", \"" + recipientUsername + "\"],\n";
                outFile << "  \"created_date\": \"" + std::string(timeStr) + "\",\n";
                outFile << "  \"messages\": [\n";
                outFile << newMessage << "\n";
                outFile << "  ]\n";
                outFile << "}\n";

                std::cout << "[SERVER] Created new chat file: " << chatFileName << "\n";
            }
            else {
                // Parse existing JSON and append new message
                try {
                    simdjson::dom::parser parser;
                    simdjson::dom::element doc;
                    auto error = parser.parse(existingContent).get(doc);

                    if (error == simdjson::SUCCESS) {
                        // Valid JSON, append new message to messages array
                        size_t messagesEndPos = existingContent.rfind("  ]");
                        if (messagesEndPos != std::string::npos) {
                            // Check if messages array exists
                            size_t messagesStartPos = existingContent.find("\"messages\": [");
                            if (messagesStartPos != std::string::npos) {
                                std::string messagesSection = existingContent.substr(
                                    messagesStartPos + 13, messagesEndPos - messagesStartPos - 13);

                                // Check if there are existing messages in the array
                                bool hasExistingMessages = messagesSection.find("{") != std::string::npos;

                                if (hasExistingMessages) {
                                    // Add comma separator before new message
                                    existingContent.insert(messagesEndPos, ",\n" + newMessage + "\n");
                                }
                                else {
                                    // First message in array
                                    existingContent.insert(messagesEndPos, newMessage + "\n");
                                }
                            }
                        }
                        outFile << existingContent;
                    }
                    else {
                        throw std::runtime_error("Invalid JSON structure");
                    }
                }
                catch (const std::exception& e) {
                    // If JSON is corrupted, recreate the file
                    std::cerr << "[SERVER] JSON corrupted, recreating file: " << e.what() << "\n";
                    outFile << "{\n";
                    outFile << "  \"conversation_id\": \"" + conversationID + "\",\n";
                    outFile << "  \"participants\": [\"" + senderUsername + "\", \"" + recipientUsername + "\"],\n";
                    outFile << "  \"created_date\": \"" + std::string(timeStr) + "\",\n";
                    outFile << "  \"messages\": [\n";
                    outFile << newMessage << "\n";
                    outFile << "  ]\n";
                    outFile << "}\n";
                }
            }

            outFile.close();
            std::cout << "[SERVER] Chat message saved to " << chatFileName
                << " with ID=" << timestamp << "\n";
        }
        else {
            std::cerr << "[SERVER] Failed to open chat file for writing: " << chatFileName << "\n";
        }

    }
    catch (const std::exception& e) {
        std::cerr << "[SERVER] Error saving chat message: " << e.what() << "\n";
    }
}
protected:
    virtual bool onClientConnect(std::shared_ptr<olc::net::connection<CustomMsgTypes>> client) override
    {
        std::cout << "[SERVER] New client connecting with temporary ID=" << client->getID() << "\n";

        // When connecting, we now DO NOT send ID to the client
        // They will receive their permanent ID only after registration/authorization
        olc::net::message<CustomMsgTypes> msg;
        msg.header.id = CustomMsgTypes::ServerAccept;

        // Temporary ID will be used by the server only
        client->send(msg);
        std::cout << "[SERVER] Sent ServerAccept to temporary client\n";

        // Sending invitation to register or log in
        SendMessageToClient(client, "Please register or log in to get access to server features");

        return true;
    }

    virtual void onClientDisconnect(std::shared_ptr<olc::net::connection<CustomMsgTypes>> client) override
    {
        uint32_t clientID = client->getID();
        std::cout << "[SERVER] Client disconnecting: ID=" << clientID << "\n";

        // Check if the client is in the list of authenticated users
        std::string username;
        bool isAuthenticated = false;

        {
            // Protect access to authenticated users lists with mutex
            std::lock_guard<std::mutex> lock(authMutex);

            auto it = authenticatedUsers.find(clientID);
            if (it != authenticatedUsers.end()) {
                username = it->second;
                isAuthenticated = true;

                // Update user online status to offline
                userManager.setUserOnlineStatus(username, false);

                // Remove the connection from both mappings
                userToClientMap.erase(username);
                authenticatedUsers.erase(clientID);

                std::cout << "[SERVER] User " << username << " (Client #" << clientID << ") disconnected\n";
            }
        } // Mutex is released here

        // Notify other clients outside of mutex lock to avoid deadlock
        if (isAuthenticated) {
            std::string disconnectMsg = "User " + username + " disconnected";
            BroadcastMessage(disconnectMsg);
        }
        else {
            std::cout << "[SERVER] Unauthenticated client disconnected: ID=" << clientID << "\n";
        }
    }


    virtual void onMessage(std::shared_ptr<olc::net::connection<CustomMsgTypes>> client, olc::net::message<CustomMsgTypes>& msg) override
    {
        std::cout << "[SERVER] Message received from client ID=" << client->getID()
            << ", MsgID=" << static_cast<uint32_t>(msg.header.id)
            << ", Size=" << msg.header.size << "\n";

        // Reset read position before processing message to ensure proper data extraction
        msg.reset_read_position();

        switch (msg.header.id)
        {



        case CustomMsgTypes::GlobalMessage:
        {
            std::cout << "[SERVER] Processing GlobalMessage from client ID=" << client->getID() << "\n";

            // Check if the sender is authenticated
            std::string senderUsername;
            bool senderAuthenticated = false;

            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = authenticatedUsers.find(client->getID());
                if (it != authenticatedUsers.end()) {
                    senderUsername = it->second;
                    senderAuthenticated = true;
                }
            }

            if (!senderAuthenticated) {
                SendMessageToClient(client, "Error: You must be logged in to send global messages");
                break;
            }

            // Get the sender's user ID
            uint32_t senderUserID = userManager.getUserID(senderUsername);

            // Extract message size from the packet
            uint32_t messageSize = 0;
            msg >> messageSize;

            if (messageSize > 10000) { // Size limit check
                std::cerr << "[SERVER] Global message size too large: " << messageSize << std::endl;
                break;
            }

            // Read the message text character by character
            std::string messageText;
            messageText.reserve(messageSize);
            for (uint32_t i = 0; i < messageSize; i++) {
                char c;
                msg >> c;
                messageText.push_back(c);
            }

            std::cout << "[SERVER] User " << senderUsername
                << " sent global message: " << messageText << "\n";

            // Save the message to persistent storage
            saveGlobalMessage(senderUsername, senderUserID, messageText);

            // Broadcast the message to all authenticated users
            olc::net::message<CustomMsgTypes> globalMsg;
            globalMsg.header.id = CustomMsgTypes::GlobalMessage;

            // Pack sender's user ID
            globalMsg << senderUserID;

            // Pack message size and content
            uint32_t fullSize = static_cast<uint32_t>(messageText.size());
            globalMsg << fullSize;
            for (const char& c : messageText) {
                globalMsg << c;
            }

            // Send to all authenticated clients except the sender
            {
                std::lock_guard<std::mutex> lock(authMutex);
                for (const auto& pair : authenticatedUsers) {
                    uint32_t clientID = pair.first;
                    if (clientID != client->getID()) { // Don't send to sender
                        auto recipient = getClientByID(clientID);
                        if (recipient && recipient->isConnected()) {
                            recipient->send(globalMsg);
                        }
                    }
                }
            }

            // Send confirmation to the sender
            SendMessageToClient(client, "Your global message has been sent to all users");
            std::cout << "[SERVER] Global message broadcasted to all users\n";
        }
        break;

        case CustomMsgTypes::GlobalChatHistoryRequest:
        {
            std::cout << "[SERVER] Processing GlobalChatHistoryRequest from client ID=" << client->getID() << "\n";

            // Verify that the requester is authenticated
            std::string requesterUsername;
            bool requesterAuthenticated = false;

            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = authenticatedUsers.find(client->getID());
                if (it != authenticatedUsers.end()) {
                    requesterUsername = it->second;
                    requesterAuthenticated = true;
                }
            }

            if (!requesterAuthenticated) {
                SendMessageToClient(client, "Error: You must be logged in to request global chat history");
                break;
            }

            std::cout << "[SERVER] User " << requesterUsername << " requested global chat history\n";

            // Load and format the global chat history
            std::string rawGlobalChatHistory = loadGlobalChatHistory();
            std::string formattedHistory = formatChatHistory(rawGlobalChatHistory);

            // Prepare the response message
            olc::net::message<CustomMsgTypes> historyResponse;
            historyResponse.header.id = CustomMsgTypes::GlobalChatHistoryResponse;

            // Pack the formatted history size and content as string
            uint32_t historySize = static_cast<uint32_t>(formattedHistory.size());
            historyResponse << historySize;

            for (const char& c : formattedHistory) {
                historyResponse << c;
            }

            // Send the history to the requesting client
            client->send(historyResponse);

            std::cout << "[SERVER] Formatted global chat history sent to " << requesterUsername
                << " (size: " << historySize << " bytes)\n";
        }
        break;
        case CustomMsgTypes::ChatRequest:
        {
            std::cout << "[SERVER] Processing ChatRequest from client ID=" << client->getID() << "\n";

            // Verify that the sender is authenticated
            std::string senderUsername;
            bool senderAuthenticated = false;

            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = authenticatedUsers.find(client->getID());
                if (it != authenticatedUsers.end()) {
                    senderUsername = it->second;
                    senderAuthenticated = true;
                }
            }

            if (!senderAuthenticated) {
                SendMessageToClient(client, "Error: You must be logged in to send chat requests");
                break;
            }

            // Extract the recipient's user ID from the message
            uint32_t recipientUserID = 0;
            msg >> recipientUserID;

            std::cout << "[SERVER] User " << senderUsername
                << " sent chat request to UserID #" << recipientUserID << "\n";

            // Find the recipient connection by user ID
            std::shared_ptr<olc::net::connection<CustomMsgTypes>> recipient = nullptr;
            std::string recipientUsername;

            {
                std::lock_guard<std::mutex> lock(authMutex);

                // Search through authenticated users to find the one with matching user ID
                for (auto it = authenticatedUsers.begin(); it != authenticatedUsers.end(); ++it) {
                    uint32_t clientID = it->first;
                    std::string username = it->second;

                    uint32_t userID = userManager.getUserID(username);
                    if (userID == recipientUserID) {
                        recipient = getClientByID(clientID);
                        recipientUsername = username;
                        break;
                    }
                }
            }

            if (recipient != nullptr && recipient->isConnected()) {
                // Forward the chat request to the recipient
                olc::net::message<CustomMsgTypes> chatRequestMsg;
                chatRequestMsg.header.id = CustomMsgTypes::ChatRequest;

                // Pack the sender's user ID
                uint32_t senderUserID = userManager.getUserID(senderUsername);
                chatRequestMsg << senderUserID;

                // Send the request to the recipient
                recipient->send(chatRequestMsg);
                std::cout << "[SERVER] Chat request forwarded to user " << recipientUsername
                    << " (UserID #" << recipientUserID << ")\n";

                // Send confirmation to the sender
                SendMessageToClient(client, "Chat request sent to " + recipientUsername);
            }
            else {
                // Recipient not found or offline
                SendMessageToClient(client, "Error: User with ID #" + std::to_string(recipientUserID) + " not found or offline");
                std::cout << "[SERVER] Failed to forward chat request: UserID #" << recipientUserID << " not found or offline\n";
            }
        }
        break;
        case CustomMsgTypes::ChatResponse:
        {
            std::cout << "[SERVER] Processing ChatResponse from client ID=" << client->getID() << "\n";

            // Check if the sender is authenticated
            std::string senderUsername;
            bool senderAuthenticated = false;

            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = authenticatedUsers.find(client->getID());
                if (it != authenticatedUsers.end()) {
                    senderUsername = it->second;
                    senderAuthenticated = true;
                }
            }

            if (!senderAuthenticated) {
                SendMessageToClient(client, "Error: You must be logged in to respond to chat requests");
                break;
            }

            // Extract the recipient user ID (the one who sent the request)
            uint32_t recipientUserID = 0;
            msg >> recipientUserID;

            // Extract the response (accept/decline)
            bool accepted = false;
            msg >> accepted;

            std::cout << "[SERVER] User " << senderUsername
                << " responded to chat request from UserID #" << recipientUserID
                << " with answer: " << (accepted ? "ACCEPTED" : "DECLINED") << "\n";

            // Find the recipient client (the one who sent the request)
            std::shared_ptr<olc::net::connection<CustomMsgTypes>> recipient = nullptr;
            std::string recipientUsername;

            { 
                std::lock_guard<std::mutex> lock(authMutex);

                // Find the authenticated user whose userID matches recipientUserID
                for (auto it = authenticatedUsers.begin(); it != authenticatedUsers.end(); ++it) {
                    uint32_t clientID = it->first;
                    std::string username = it->second;

                    uint32_t userID = userManager.getUserID(username);
                    if (userID == recipientUserID) {
                        recipient = getClientByID(clientID);
                        recipientUsername = username;
                        break;
                    }
                }
            }

            if (recipient != nullptr && recipient->isConnected()) {
                // Create response message for the original requester
                olc::net::message<CustomMsgTypes> chatResponseMsg;
                chatResponseMsg.header.id = CustomMsgTypes::ChatResponse;

                // Add the sender's user ID (the one who accepted/declined)
                uint32_t senderUserID = userManager.getUserID(senderUsername);
                chatResponseMsg << senderUserID;

                // Add the response status
                chatResponseMsg << accepted;

                // Send the response to the recipient
                recipient->send(chatResponseMsg);
                std::cout << "[SERVER] Chat response forwarded to user " << recipientUsername
                    << " (UserID #" << recipientUserID << ")\n";

                // If accepted, automatically send chat history to both users
                if (accepted) {
                    // Load raw chat history from file
                    std::string rawChatHistory = loadChatHistory(senderUsername, recipientUsername);

                    if (!rawChatHistory.empty()) {
                        // Format history for display
                        std::string formattedHistory = formatChatHistory(rawChatHistory);

                        // Send history to the first user (responder)
                        olc::net::message<CustomMsgTypes> historyMsg1;
                        historyMsg1.header.id = CustomMsgTypes::ChatHistoryResponse;
                        historyMsg1 << recipientUserID; // ID of the chat partner

                        uint32_t historySize1 = static_cast<uint32_t>(formattedHistory.size());
                        historyMsg1 << historySize1;
                        for (const char& c : formattedHistory) {
                            historyMsg1 << c;
                        }
                        client->send(historyMsg1);

                        // Send the same history to the second user (original requester)
                        olc::net::message<CustomMsgTypes> historyMsg2;
                        historyMsg2.header.id = CustomMsgTypes::ChatHistoryResponse;
                        historyMsg2 << senderUserID; // ID of the chat partner

                        uint32_t historySize2 = static_cast<uint32_t>(formattedHistory.size());
                        historyMsg2 << historySize2;
                        for (const char& c : formattedHistory) {
                            historyMsg2 << c;
                        }
                        recipient->send(historyMsg2);

                        std::cout << "[SERVER] Chat history automatically sent to both users (size: " << historySize1 << " bytes)\n";
                    }
                    else {
                        std::cout << "[SERVER] No chat history found between " << senderUsername << " and " << recipientUsername << "\n";

                        // Send empty history notification
                        std::string emptyHistory = "\n=== CHAT HISTORY ===\nNo previous messages found.\n=== END OF HISTORY ===\n";

                        olc::net::message<CustomMsgTypes> emptyMsg1;
                        emptyMsg1.header.id = CustomMsgTypes::ChatHistoryResponse;
                        emptyMsg1 << recipientUserID;
                        uint32_t emptySize = static_cast<uint32_t>(emptyHistory.size());
                        emptyMsg1 << emptySize;
                        for (const char& c : emptyHistory) {
                            emptyMsg1 << c;
                        }
                        client->send(emptyMsg1);

                        olc::net::message<CustomMsgTypes> emptyMsg2;
                        emptyMsg2.header.id = CustomMsgTypes::ChatHistoryResponse;
                        emptyMsg2 << senderUserID;
                        emptyMsg2 << emptySize;
                        for (const char& c : emptyHistory) {
                            emptyMsg2 << c;
                        }
                        recipient->send(emptyMsg2);
                    }
                }

                // Send confirmation message to the responder
                if (accepted) {
                    SendMessageToClient(client, "You accepted chat request from " + recipientUsername);
                }
                else {
                    SendMessageToClient(client, "You declined chat request from " + recipientUsername);
                }
            }
            else {
                // Recipient not found or offline
                SendMessageToClient(client, "Error: User with ID #" + std::to_string(recipientUserID) + " not found or offline");
                std::cout << "[SERVER] Failed to forward chat response: UserID #" << recipientUserID << " not found or offline\n";
            }
        }
        break;

        case CustomMsgTypes::ChatHistoryRequest:
        {
            std::cout << "[SERVER] Processing ChatHistoryRequest from client ID=" << client->getID() << "\n";

            // Check if the requester is authenticated
            std::string requesterUsername;
            bool requesterAuthenticated = false;

            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = authenticatedUsers.find(client->getID());
                if (it != authenticatedUsers.end()) {
                    requesterUsername = it->second;
                    requesterAuthenticated = true;
                }
            }

            if (!requesterAuthenticated) {
                SendMessageToClient(client, "Error: You must be logged in to request chat history");
                break;
            }

            // Extract the other user's ID whose chat history is requested
            uint32_t otherUserID = 0;
            msg >> otherUserID;

            std::cout << "[SERVER] User " << requesterUsername
                << " requested chat history with UserID #" << otherUserID << "\n";

            // Get the other user's username by their ID
            std::string otherUsername = userManager.getUsernameByID(otherUserID);

            if (otherUsername.empty()) {
                SendMessageToClient(client, "Error: User with ID #" + std::to_string(otherUserID) + " not found");
                std::cout << "[SERVER] UserID #" << otherUserID << " not found in database\n";
                break;
            }

            // Load and format chat history
            std::string rawChatHistory = loadChatHistory(requesterUsername, otherUsername);
            std::string formattedHistory = formatChatHistory(rawChatHistory);

            // Create response message
            olc::net::message<CustomMsgTypes> historyResponse;
            historyResponse.header.id = CustomMsgTypes::ChatHistoryResponse;

            // Add the other user's ID (chat partner)
            historyResponse << otherUserID;

            // Add the formatted history size and content
            uint32_t historySize = static_cast<uint32_t>(formattedHistory.size());
            historyResponse << historySize;

            for (const char& c : formattedHistory) {
                historyResponse << c;
            }

            // Send history to the requester
            client->send(historyResponse);

            std::cout << "[SERVER] Formatted chat history sent to " << requesterUsername
                << " with " << otherUsername << " (size: " << historySize << " bytes)\n";
        }
        break;
        case CustomMsgTypes::DirectMessage:
        {
            // Check if the sender is authenticated
            std::string senderUsername;
            bool senderAuthenticated = false;

            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = authenticatedUsers.find(client->getID());
                if (it != authenticatedUsers.end()) {
                    senderUsername = it->second;
                    senderAuthenticated = true;
                }
            }

            if (!senderAuthenticated) {
                SendMessageToClient(client, "Error: You must be logged in to send private messages");
                break;
            }

            // Get the sender's user ID
            uint32_t senderUserID = userManager.getUserID(senderUsername);

            // Extract recipient's user ID
            uint32_t recipientUserID = 0;
            msg >> recipientUserID;

            // Extract message size
            uint32_t messageSize = 0;
            msg >> messageSize;

            if (messageSize > 10000) { // Size limit check
                std::cerr << "[SERVER] Direct message size too large: " << messageSize << std::endl;
                break;
            }

            // Read the message content
            std::string messageText;
            messageText.reserve(messageSize);
            for (uint32_t i = 0; i < messageSize; i++) {
                char c;
                msg >> c;
                messageText.push_back(c);
            }

            std::cout << "[SERVER] User " << senderUsername
                << " sent direct message to UserID #" << recipientUserID
                << ": " << messageText << "\n";

            // Find the recipient client by their user ID
            std::shared_ptr<olc::net::connection<CustomMsgTypes>> recipient = nullptr;
            std::string recipientUsername;

            {
                std::lock_guard<std::mutex> lock(authMutex);

                // Find the authenticated user whose userID matches recipientUserID
                for (auto it = authenticatedUsers.begin(); it != authenticatedUsers.end(); ++it) {
                    uint32_t clientID = it->first;
                    std::string username = it->second;

                    uint32_t userID = userManager.getUserID(username);
                    if (userID == recipientUserID) {
                        recipient = getClientByID(clientID);
                        recipientUsername = username;
                        break;
                    }
                }
            }
if (recipient != nullptr && recipient->isConnected()) {
                // Save the message to chat history database
                saveChatMessage(senderUsername, senderUserID, recipientUsername, recipientUserID, messageText);

                // Create new message for the recipient
                olc::net::message<CustomMsgTypes> directMsg;
                directMsg.header.id = CustomMsgTypes::DirectMessage;

                // Add sender's user ID (required for recipient to identify sender)
                directMsg << senderUserID;

                // Pack message text size and content
                uint32_t fullSize = static_cast<uint32_t>(messageText.size());
                directMsg << fullSize;
                for (const char& c : messageText) {
                    directMsg << c;
                }

                // Send the message to recipient
                recipient->send(directMsg);
                std::cout << "[SERVER] Direct message forwarded to user " << recipientUsername
                    << " (UserID #" << recipientUserID << ")\n";

                // Confirm delivery to sender
                SendMessageToClient(client, "Your message has been delivered to " + recipientUsername);
            }
            else {
                // Recipient not found or offline
                SendMessageToClient(client, "Error: User with ID #" + std::to_string(recipientUserID) + " not found or offline");
                std::cout << "[SERVER] Failed to forward message: UserID #" << recipientUserID << " not found or offline\n";
            }
        }
        break;


        case CustomMsgTypes::RequestClientList:
        {
            std::cout << "[SERVER] Client #" << client->getID() << " requested client list\n";

            // Build list of all connected clients
            std::string clientList = "Connected clients:";

            // Thread-safe access to authenticated users list
            std::lock_guard<std::mutex> lock(authMutex);

            // Iterate through all active connections
            for (auto& conn : getAllClients()) {
                if (conn && conn->isConnected()) {
                    uint32_t connID = conn->getID();
                    std::string info = " #" + std::to_string(connID);

                    // Add username if client is authenticated
                    auto it = authenticatedUsers.find(connID);
                    if (it != authenticatedUsers.end()) {
                        info += " (" + it->second + ")";
                    }

                    clientList += info + ",";
                }
            }

            // Remove trailing comma if present
            if (clientList.back() == ',') {
                clientList.pop_back();
            }

            std::cout << "[SERVER] Sending client list to client #" << client->getID() << ": " << clientList << "\n";

            // Send the client list back to requester
            SendMessageToClient(client, clientList);
        }
        break;

        case CustomMsgTypes::RegisterRequest:
        {
            std::cout << "[SERVER] Processing RegisterRequest from client ID=" << client->getID() << "\n";
            // Extract username from message
            uint32_t usernameSize = 0;
            msg >> usernameSize;
            std::string username;
            for (uint32_t i = 0; i < usernameSize && i < 100; i++) { // Username size limit for security
                char c;
                msg >> c;
                username.push_back(c);
            }
            // Extract password from message
            uint32_t passwordSize = 0;
            msg >> passwordSize;
            std::string password;
            for (uint32_t i = 0; i < passwordSize && i < 100; i++) { // Password size limit for security
                char c;
                msg >> c;
                password.push_back(c);
            }
            // Extract email from message
            uint32_t emailSize = 0;
            msg >> emailSize;
            std::string email;
            for (uint32_t i = 0; i < emailSize && i < 100; i++) { // Email size limit for security
                char c;
                msg >> c;
                email.push_back(c);
            }
            std::cout << "[SERVER] Registration/Login attempt for username: " << username << ", email: " << email << "\n";

            // Check if user already exists in database
            bool userExists = userManager.doesUserExist(username);
            bool success = false;
            std::string responseMessage;

            // Check if user is already logged in from another client
            bool userOnline = false;
            uint32_t existingClientID = 0;
            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = userToClientMap.find(username);
                if (it != userToClientMap.end()) {
                    userOnline = true;
                    existingClientID = it->second;
                }
            }

            if (userExists) {
                // User exists - attempt login with provided credentials
                success = userManager.authenticateUser(username, password);

                if (success) {
                    if (userOnline) {
                        // Handle multiple login scenario - disconnect previous session
                        responseMessage = "User " + username + " is already authorized from another client (#" +
                            std::to_string(existingClientID) + "). Previous session will be terminated.";
                        std::cout << "[SERVER] User " << username << " is already online. Handling multiple login." << "\n";

                        // Prepare response message before disconnecting previous client
                        olc::net::message<CustomMsgTypes> response;
                        response.header.id = CustomMsgTypes::RegisterResponse;
                        response << success;

                        // Pack response message
                        uint32_t messageSize = static_cast<uint32_t>(responseMessage.size());
                        response << messageSize;
                        for (const char& c : responseMessage) {
                            response << c;
                        }

                        // Send response to new client attempting to login
                        client->send(response);

                        // Get or assign permanent user ID
                        uint32_t userID = userManager.getUserID(username);
                        if (userID == 0) {
                            // Assign new permanent ID if user doesn't have one yet
                            userID = userManager.assignUserID(username);
                        }

                        // Update authentication mappings with thread safety
                        {
                            std::lock_guard<std::mutex> lock(authMutex);
                            authenticatedUsers[client->getID()] = username;
                            userToClientMap[username] = client->getID();
                        }

                        // Send permanent user ID to authenticated client
                        olc::net::message<CustomMsgTypes> idMsg;
                        idMsg.header.id = CustomMsgTypes::ServerAccept;
                        idMsg << userID;
                        client->send(idMsg);

                        std::cout << "[SERVER] User " << username << " authenticated with permanent ID=" << userID << "\n";

                        // Find and disconnect the previous client session
                        auto oldClient = getClientByID(existingClientID);
                        if (oldClient && oldClient->isConnected()) {
                            SendMessageToClient(oldClient, "You have been disconnected because your account was opened from another device");
                            std::cout << "[SERVER] Sending notification to client #" << existingClientID << " about new login" << "\n";

                            // Clean up authentication data for old client
                            {
                                std::lock_guard<std::mutex> lock(authMutex);
                                authenticatedUsers.erase(existingClientID);
                            }

                            // Safely disconnect old client in separate thread to avoid blocking
                            std::thread([this, existingClientID]() {
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                auto client = this->getClientByID(existingClientID);
                                if (client) {
                                    this->removeClient(client);
                                }
                                }).detach();
                        }

                        // Response already sent, exit handler
                        break;
                    }
                    else {
                        // Single login scenario - user authenticated successfully
                        responseMessage = "User already exists. Automatic login performed. Welcome, " + username + "!";
                    }
                    std::cout << "[SERVER] User " << username << " exists. Auto-login successful." << "\n";
                }
                else {
                    // Authentication failed - wrong password
                    responseMessage = "User already exists, but password is incorrect. Please try again.";
                    std::cout << "[SERVER] User " << username << " exists but authentication failed." << "\n";
                }
            }
            else {
                // User doesn't exist - proceed with registration
                // Create new user object
                User newUser;
                newUser.username = username;
                newUser.password_hash = userManager.hashPassword(password);
                newUser.email = email;

                // Get current timestamp for registration date
                auto now = std::chrono::system_clock::now();
                time_t time_now = std::chrono::system_clock::to_time_t(now);
                char timeStr[100];
                struct tm timeinfo;

                // Use thread-safe time conversion
#ifdef _WIN32
                localtime_s(&timeinfo, &time_now);  // Windows secure version
#else
                localtime_r(&time_now, &timeinfo);  // POSIX thread-safe version
#endif

                std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
                newUser.registration_date = timeStr;

                // Attempt to register new user in database
                success = userManager.registerUser(newUser);
                responseMessage = success ?
                    "Registration successful. Welcome, " + username + "!" :
                    "Registration failed. Please try again.";
            }

            // Prepare response message for client
            olc::net::message<CustomMsgTypes> response;
            response.header.id = CustomMsgTypes::RegisterResponse;

            // Add success/failure flag
            response << success;

            // Pack response message text
            uint32_t messageSize = static_cast<uint32_t>(responseMessage.size());
            response << messageSize;
            for (const char& c : responseMessage) {
                response << c;
            }

            // Send response back to client
            client->send(response);

            // Rest of code for handling successful registration/login...
            // (leave as was)
        }
        break;
case CustomMsgTypes::LoginRequest:
        {
            std::cout << "[SERVER] Processing LoginRequest from client ID=" << client->getID() << "\n";

            // Extract username size from message
            uint32_t usernameSize = 0;
            msg >> usernameSize;

            // Read username character by character
            std::string username;
            for (uint32_t i = 0; i < usernameSize; i++) {
                char c;
                msg >> c;
                username.push_back(c);
            }

            // Extract password size from message
            uint32_t passwordSize = 0;
            msg >> passwordSize;

            // Read password character by character
            std::string password;
            for (uint32_t i = 0; i < passwordSize; i++) {
                char c;
                msg >> c;
                password.push_back(c);
            }

            std::cout << "[SERVER] Login attempt for username: " << username << "\n";

            // Check if user is already logged in from another client
            bool userOnline = false;
            uint32_t existingClientID = 0;
            {
                std::lock_guard<std::mutex> lock(authMutex);
                auto it = userToClientMap.find(username);
                if (it != userToClientMap.end()) {
                    userOnline = true;
                    existingClientID = it->second;
                }
            }

            // Verify user credentials with user manager
            bool success = userManager.authenticateUser(username, password);
            std::string responseMessage;

            if (success && userOnline) {
                responseMessage = "User " + username + " already logged in from another client (#" +
                    std::to_string(existingClientID) + "). Previous session will be terminated.";
                std::cout << "[SERVER] Existing session detected for " << username << ", Client #" << existingClientID << "\n";

                // Locate and disconnect the previous client session
                auto oldClient = getClientByID(existingClientID);
                if (oldClient && oldClient->isConnected()) {
                    SendMessageToClient(oldClient, "You have been disconnected because your account was opened from another device");
                    std::cout << "[SERVER] Sending notification to client #" << existingClientID << " about new login" << "\n";

                    // Remove authentication data for the old client
                    {
                        std::lock_guard<std::mutex> lock(authMutex);
                        authenticatedUsers.erase(existingClientID);
                        userToClientMap.erase(username);
                    }

                    // Asynchronously remove the old client after a short delay
                    std::thread([this, existingClientID]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        auto client = this->getClientByID(existingClientID);
                        if (client) {
                            this->removeClient(client);
                        }
                        }).detach();
                }
            }
            else if (success) {
                responseMessage = "Login successful. Welcome back, " + username + "!";
            }
            else {
                responseMessage = "Login failed. Invalid username or password.";
            }

            // Prepare login response message
            olc::net::message<CustomMsgTypes> response;
            response.header.id = CustomMsgTypes::LoginResponse;

            // Add success flag to response
            response << success;

            // Add response message size and content
            uint32_t messageSize = static_cast<uint32_t>(responseMessage.size());
            response << messageSize;

            for (const char& c : responseMessage) {
                response << c;
            }

            // Send login response to client
            client->send(response);

            if (success) {
                // Update authentication mappings with thread safety
                {
                    std::lock_guard<std::mutex> lock(authMutex);
                    authenticatedUsers[client->getID()] = username;
                    userToClientMap[username] = client->getID();
                }

                // Retrieve user's permanent ID from user manager
                uint32_t userID = userManager.getUserID(username);

                // Send permanent user ID to client
                olc::net::message<CustomMsgTypes> idMsg;
                idMsg.header.id = CustomMsgTypes::ServerAccept;
                idMsg << userID;
                client->send(idMsg);

                std::cout << "[SERVER] User " << username << " logged in with permanent ID=" << userID << "\n";

                // Update user's online status in user manager
                userManager.setUserOnlineStatus(username, true, userID);

                // Notify all other clients about the new user login
                BroadcastMessage("User " + username + " has logged in", client);
            }
        }
        break;

        default:
            std::cout << "[SERVER] Unknown message type: " << static_cast<uint32_t>(msg.header.id) << "\n";
            break;
        }
    }
};

int main()
{
    std::cout << "[SERVER] Starting on port 60000...\n";

    try {
        // Initialize custom server on port 60000
        CustomServer server(60000);

        // Attempt to start the server
        if (server.start()) {
            std::cout << "[SERVER] Started successfully!\n";
        }
        else {
            std::cout << "[SERVER] Failed to start!\n";
            return -1;
        }

        std::cout << "[SERVER] Entering main loop...\n";
        std::cout << "[SERVER] Press Ctrl+C to stop server\n";

        // Main server event loop
        bool running = true;
        while (running) {
            try {
                // Process all pending messages (-1 = process all, true = wait if queue empty)
                server.update(-1, true);

                // Small delay to prevent excessive CPU usage
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            catch (const std::exception& e) {
                std::cerr << "[SERVER] Error in main loop: " << e.what() << "\n";
                // Continue running despite errors to maintain server stability
            }
        }

        // Graceful server shutdown
        std::cout << "[SERVER] Shutting down...\n";
        server.stop();
    }
    catch (const std::exception& e) {
        std::cerr << "[SERVER] Fatal error: " << e.what() << "\n";
        return -1;
    }

    return 0;
}