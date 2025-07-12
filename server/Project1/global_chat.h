#ifndef GLOBAL_CHAT_H
#define GLOBAL_CHAT_H

#include <string>
#include <mutex>
#include "net_common.h"
#include "net_message.h"

class GlobalChatManager
{
private:
    // Mutex for thread-safe access to global chat operations
    std::mutex globalChatMutex;

public:
    // Method for saving global chat messages to persistent storage
    void saveGlobalMessage(const std::string& senderUsername, uint32_t senderUserID, const std::string& messageText);

    // Method for loading and retrieving global chat history from storage
    std::string loadGlobalChatHistory();
};

#endif // GLOBAL_CHAT_H