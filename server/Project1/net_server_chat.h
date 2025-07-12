#pragma once
#include "net_server.h"
#include <mutex>
#include <fstream>
#include <chrono>
#include <ctime>

namespace olc
{
    namespace net
    {
        template<typename T>
        class server_chat_interface
        {
        protected:
            std::mutex chatLogMutex; // Mutex for thread-safe access to chat log files

        public:
            // Method to extract only messages from full chat history without timestamps and metadata
            std::string extractMessagesOnly(const std::string& fullChatHistory);

            // Generates unique chat file name for communication between two specific users
            std::string generateChatFileName(const std::string& user1, const std::string& user2);

            // Method to load existing chat history between two users from persistent storage
            std::string loadChatHistory(const std::string& user1, const std::string& user2);

            // Helper method to send a message to a specific client connection
            void SendMessageToClient(std::shared_ptr<olc::net::connection<CustomMsgTypes>> client, const std::string& message);

            // Helper method to broadcast a message to all connected clients except the excluded one
            void BroadcastMessage(const std::string& message, std::shared_ptr<olc::net::connection<CustomMsgTypes>> excludeClient = nullptr);

            // Method to extract only messages from global chat history without timestamps and metadata
            std::string extractGlobalMessagesOnly(const std::string& fullGlobalHistory);
        };
    }
}