#include "net_server_chat.h"
#include <iostream>
#include "simdjson.h"

namespace olc
{
    namespace net
    {
        template<typename T>
        std::string server_chat_interface<T>::extractMessagesOnly(const std::string& fullChatHistory) {
            try {
                // Parse JSON using simdjson library
                simdjson::ondemand::parser parser;
                simdjson::ondemand::document doc = parser.iterate(fullChatHistory);

                std::string result = "=== Chat History ===\n\n";

                // Extract messages array from JSON
                auto messages = doc["messages"];

                // Check if there are any messages
                bool hasMessages = false;
                for (auto message : messages) {
                    hasMessages = true;

                    // Extract message fields
                    std::string_view senderUsernameView = message["sender_username"].get_string();
                    std::string senderUsername(senderUsernameView);

                    std::string_view messageTextView = message["message_text"].get_string();
                    std::string messageText(messageTextView);

                    uint64_t messageId = message["message_id"].get_uint64();

                    // Extract timestamp from message_id and convert to time format
                    auto timestamp = std::chrono::milliseconds(messageId);
                    auto timePoint = std::chrono::time_point<std::chrono::system_clock>(timestamp);
                    time_t time_now = std::chrono::system_clock::to_time_t(timePoint);

                    char timeStr[100];
                    struct tm timeinfo;
#ifdef _WIN32
                    localtime_s(&timeinfo, &time_now);
#else
                    localtime_r(&time_now, &timeinfo);
#endif
                    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

                    // Format message in readable form
                    result += "[" + std::string(timeStr) + "] " + senderUsername + ": " + messageText + "\n";
                }

                if (!hasMessages) {
                    result += "No messages in this chat yet.\n";
                }

                result += "\n=== End of History ===";
                return result;

            }
            catch (const std::exception& e) {
                std::cerr << "[SERVER] Error parsing chat history: " << e.what() << "\n";
                return "Error loading chat history. Please try again.";
            }
        }

        template<typename T>
        std::string server_chat_interface<T>::generateChatFileName(const std::string& user1, const std::string& user2) {
            // Sort usernames alphabetically for consistent file naming
            std::string sortedNames;
            if (user1 < user2) {
                sortedNames = user1 + "_" + user2;
            }
            else {
                sortedNames = user2 + "_" + user1;
            }
            return "chat_" + sortedNames + ".json";
        }

        template<typename T>
        std::string server_chat_interface<T>::loadChatHistory(const std::string& user1, const std::string& user2) {
            std::lock_guard<std::mutex> lock(chatLogMutex);

            try {
                // Generate filename for the chat between two users
                std::string chatFileName = generateChatFileName(user1, user2);

                // Check if file exists
                std::ifstream inFile(chatFileName);
                if (!inFile.is_open()) {
                    std::cout << "[SERVER] Chat history file not found: " << chatFileName << "\n";
                    return "{\"messages\": []}"; // Return empty message array
                }

                // Read entire file content
                std::string content;
                std::string line;
                while (std::getline(inFile, line)) {
                    content += line + "\n";
                }
                inFile.close();

                if (content.empty()) {
                    std::cout << "[SERVER] Chat history file is empty: " << chatFileName << "\n";
                    return "{\"messages\": []}";
                }

                // Extract only messages and return in simplified format
                std::string simplifiedHistory = extractMessagesOnly(content);

                std::cout << "[SERVER] Loaded and simplified chat history from: " << chatFileName << "\n";
                return simplifiedHistory;

            }
            catch (const std::exception& e) {
                std::cerr << "[SERVER] Error loading chat history: " << e.what() << "\n";
                return "{\"messages\": []}";
            }
        }

        template<typename T>
        void server_chat_interface<T>::SendMessageToClient(std::shared_ptr<olc::net::connection<CustomMsgTypes>> client, const std::string& message)
        {
            if (client && client->isConnected()) {
                olc::net::message<CustomMsgTypes> msg;
                msg.header.id = CustomMsgTypes::ServerMessage;

                // Write message size to packet
                uint32_t messageSize = static_cast<uint32_t>(message.size());
                msg << messageSize;

                // Write message content character by character
                for (const char& c : message)
                {
                    msg << c;
                }

                client->send(msg);
            }
        }

        template<typename T>
        void server_chat_interface<T>::BroadcastMessage(const std::string& message, std::shared_ptr<olc::net::connection<CustomMsgTypes>> excludeClient)
        {
            olc::net::message<CustomMsgTypes> msg;
            msg.header.id = CustomMsgTypes::MessageAll;

            // Write message size to packet
            uint32_t messageSize = static_cast<uint32_t>(message.size());
            msg << messageSize;

            // Write message content character by character
            for (const char& c : message)
            {
                msg << c;
            }

            // Here you would call messageAllClients method from the base class
            // messageAllClients(msg, excludeClient);
        }

        template<typename T>
        std::string server_chat_interface<T>::extractGlobalMessagesOnly(const std::string& fullGlobalHistory) {
            try {
                // Parse JSON using simdjson library
                simdjson::ondemand::parser parser;
                simdjson::ondemand::document doc = parser.iterate(fullGlobalHistory);

                std::string result = "=== Global Chat History ===\n\n";

                // Extract messages array from JSON
                auto messages = doc["messages"];

                // Check if there are any messages
                bool hasMessages = false;
                for (auto message : messages) {
                    hasMessages = true;

                    // Extract message fields
                    std::string_view senderUsernameView = message["sender_username"].get_string();
                    std::string senderUsername(senderUsernameView);

                    std::string_view messageTextView = message["message_text"].get_string();
                    std::string messageText(messageTextView);

                    std::string_view timestampView = message["timestamp"].get_string();
                    std::string timestamp(timestampView);

                    // Format message in readable form
                    result += "[" + timestamp + "] " + senderUsername + ": " + messageText + "\n";
                }

                if (!hasMessages) {
                    result += "No messages in global chat yet.\n";
                }

                result += "\n=== End of History ===";
                return result;

            }
            catch (const std::exception& e) {
                std::cerr << "[SERVER] Error parsing global chat history: " << e.what() << "\n";
                return "=== Global Chat History ===\n\nError loading chat history. Please try again.\n\n=== End of History ===";
            }
        }

        // Explicit template instantiation for CustomMsgTypes
        template class server_chat_interface<CustomMsgTypes>;
    }
}