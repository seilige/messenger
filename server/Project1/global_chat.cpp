#include "global_chat.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>

void GlobalChatManager::saveGlobalMessage(const std::string& senderUsername, uint32_t senderUserID, const std::string& messageText)
{
    // Thread-safe access to global chat operations
    std::lock_guard<std::mutex> lock(globalChatMutex);

    try {
        const std::string globalChatFile = "global_chat.json";

        // Get current timestamp for message creation
        auto now = std::chrono::system_clock::now();
        time_t time_now = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        struct tm timeinfo;

#ifdef _WIN32
        localtime_s(&timeinfo, &time_now);
#else
        localtime_r(&time_now, &timeinfo);
#endif
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

        // Generate unique message ID using timestamp in milliseconds
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Read existing file content if it exists
        std::string existingContent;
        std::ifstream inFile(globalChatFile);
        bool fileExists = false;

        if (inFile.is_open()) {
            fileExists = true;
            std::string line;
            while (std::getline(inFile, line)) {
                existingContent += line + "\n";
            }
            inFile.close();
        }

        // Create new message in JSON format
        std::string newMessage = "    {\n";
        newMessage += "      \"message_id\": " + std::to_string(timestamp) + ",\n";
        newMessage += "      \"sender_username\": \"" + senderUsername + "\",\n";
        newMessage += "      \"sender_user_id\": " + std::to_string(senderUserID) + ",\n";
        newMessage += "      \"message_text\": \"" + messageText + "\",\n";
        newMessage += "      \"timestamp\": \"" + std::string(timeStr) + "\",\n";
        newMessage += "      \"message_type\": \"global_message\"\n";
        newMessage += "    }";

        // Open file for writing
        std::ofstream outFile(globalChatFile);
        if (outFile.is_open()) {
            if (!fileExists || existingContent.empty() || existingContent.find("\"messages\"") == std::string::npos) {
                // Create new chat file structure
                outFile << "{\n";
                outFile << "  \"chat_type\": \"global_chat\",\n";
                outFile << "  \"created_date\": \"" + std::string(timeStr) + "\",\n";
                outFile << "  \"messages\": [\n";
                outFile << newMessage << "\n";
                outFile << "  ]\n";
                outFile << "}\n";

                std::cout << "[GLOBAL_CHAT] Created new global chat file\n";
            }
            else {
                // Append message to existing file
                size_t insertPos = existingContent.rfind("  ]\n}");
                if (insertPos != std::string::npos) {
                    // Check if there are already messages in the array
                    size_t lastObjectPos = existingContent.rfind("    }", insertPos);
                    if (lastObjectPos != std::string::npos) {
                        // Add comma separator before new message
                        existingContent.insert(insertPos, ",\n" + newMessage + "\n");
                    }
                    else {
                        // First message in the array
                        existingContent.insert(insertPos, newMessage + "\n");
                    }
                }
                else {
                    // File structure is corrupted, recreate it
                    existingContent = "{\n";
                    existingContent += "  \"chat_type\": \"global_chat\",\n";
                    existingContent += "  \"created_date\": \"" + std::string(timeStr) + "\",\n";
                    existingContent += "  \"messages\": [\n" + newMessage + "\n  ]\n}\n";
                }

                outFile << existingContent;
            }

            outFile.close();
            std::cout << "[GLOBAL_CHAT] Global message saved with ID=" << timestamp << "\n";
        }
        else {
            std::cerr << "[GLOBAL_CHAT] Failed to open global chat file for writing\n";
        }

    }
    catch (const std::exception& e) {
        std::cerr << "[GLOBAL_CHAT] Error saving global message: " << e.what() << "\n";
    }
}

std::string GlobalChatManager::loadGlobalChatHistory()
{
    // Thread-safe access to global chat operations
    std::lock_guard<std::mutex> lock(globalChatMutex);

    try {
        const std::string globalChatFile = "global_chat.json";
        std::ifstream inFile(globalChatFile);

        if (!inFile.is_open()) {
            std::cout << "[GLOBAL_CHAT] Global chat file not found, returning empty history\n";
            return "[]"; // Return empty JSON array
        }

        // Read entire file content
        std::string content;
        std::string line;
        while (std::getline(inFile, line)) {
            content += line + "\n";
        }
        inFile.close();

        if (content.empty()) {
            std::cout << "[GLOBAL_CHAT] Global chat file is empty\n";
            return "[]";
        }

        std::cout << "[GLOBAL_CHAT] Global chat history loaded successfully\n";
        return content;

    }
    catch (const std::exception& e) {
        std::cerr << "[GLOBAL_CHAT] Error loading global chat history: " << e.what() << "\n";
        return "[]";
    }
}