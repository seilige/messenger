#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include "simdjson.h"

struct User {
    uint32_t id;         // Unique user ID
    std::string username;
    std::string password_hash;
    std::string email;
    std::string registration_date;
    bool is_online;
    uint32_t client_id;  // ID of the client connection (may be temporary)

    // Default constructor
    User() : id(0), is_online(false), client_id(10000) {}
};

class UserManager {
private:
    std::vector<User> users;
    std::string database_file;
    std::mutex mutex;
    simdjson::dom::parser json_parser;

public:
    // Method to get username by user ID
    std::string getUsernameByID(uint32_t userID);
    
    // Simple password hashing (placeholder implementation for testing)
    std::string hashPassword(const std::string& password) {
        std::string hash = password; // In production, proper hashing should be implemented
        return hash;
    }

    void setUserOnlineStatus(const std::string& username, bool isOnline, uint32_t clientId = 0) {
        std::lock_guard<std::mutex> lock(mutex);

        bool found = false;
        for (auto& user : users) {
            if (user.username == username) {
                user.is_online = isOnline;
                user.client_id = clientId;
                found = true;
                break;
            }
        }

        // Warning for debugging purposes
        if (!found) {
            std::cerr << "[USER_MANAGER] Warning: attempting to set online status for non-existent user: " << username << std::endl;
        }
    }
    // In user_manager.h, in generateJsonString() method we need to include user ID
    // In user_manager.h, in UserManager class
    std::string generateJsonString() {
        std::stringstream ss;
        ss << "{\n";
        // Add last_user_id field to JSON structure
        ss << "  \"last_user_id\": " << last_user_id << ",\n";
        ss << "  \"users\": [\n";

        for (size_t i = 0; i < users.size(); i++) {
            const auto& user = users[i];
            ss << "    {\n";
            ss << "      \"id\": " << user.id << ",\n";
            ss << "      \"username\": \"" << user.username << "\",\n";
            ss << "      \"password_hash\": \"" << user.password_hash << "\",\n";
            ss << "      \"email\": \"" << user.email << "\",\n";
            ss << "      \"registration_date\": \"" << user.registration_date << "\"\n";
            ss << "    }";

            if (i < users.size() - 1) {
                ss << ",";
            }
            ss << "\n";
        }

        ss << "  ]\n}";
        std::string result = ss.str();

        return result;
    }

    // Add this field to UserManager class in private section
private:
    uint32_t last_user_id;  // Last assigned user ID

public:
    bool saveLastUserID() {
        // In user_manager.h, in UserManager class
            // We can simply use the existing saveUsers() method
            return saveUsers();
    }

    uint32_t getUserID(const std::string& username) {
        //std::lock_guard<std::mutex> lock(mutex);

        // Load current user data from file
        loadUsers();

        for (const auto& user : users) {
            if (user.username == username) {
                return user.id;
            }
        }

        return 0; // User not found
    }


    // Assign user ID
    // In user_manager.h, in UserManager class
    uint32_t assignUserID(const std::string& username) {
        //std::lock_guard<std::mutex> lock(mutex);

        // Increment last_user_id for new assignment
        last_user_id++;

        // Assign ID to user
        for (auto& user : users) {
            if (user.username == username) {
                user.id = last_user_id;
                saveUsers(); // Save changes to file, including updated last_user_id
                return last_user_id;
            }
        }

        return 0; // User not found
    }
    void updateUserLastLogin(const std::string& username) {
        //std::lock_guard<std::mutex> lock(mutex);

        // Load current user data from file
        loadUsers();

        for (auto& user : users) {
            if (user.username == username) {
                // Update last_login field if it exists
                auto now = std::chrono::system_clock::now();
                time_t time_now = std::chrono::system_clock::to_time_t(now);
                char timeStr[100];
                struct tm timeinfo;

#ifdef _WIN32
                localtime_s(&timeinfo, &time_now);  // Windows version
#else
                localtime_r(&time_now, &timeinfo);  // POSIX version
#endif

                std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
                // If we have last_login field:
                // user.last_login = timeStr;
                break;
            }
        }

        // Save updated user data
        saveUsers();
    }

    bool doesUserExist(const std::string& username) {
        //std::lock_guard<std::mutex> lock(mutex);

        // Load current user data from file
        loadUsers();

        for (const auto& user : users) {
            if (user.username == username) {
                return true;
            }
        }

        return false;
    }

    UserManager(const std::string& dbFile = "users.json") : database_file(dbFile) {
        loadUsers();
    }

    bool loadUsers() {
        std::lock_guard<std::mutex> lock(mutex);

        try {
            // Check if file exists
            std::ifstream file(database_file);
            if (!file.is_open()) {
                // File doesn't exist, create a new one
                std::cout << "[USER_MANAGER] Database file not found, creating new one" << std::endl;
                last_user_id = 10000; // Initialize starting value for last_user_id
                saveUsers();
                return true;
            }

            // Get file size for reading entire content into memory
            file.seekg(0, std::ios::end);
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
// Check if file is empty, create new one
            if (file_size == 0) {
                file.close();
                std::cout << "[USER_MANAGER] File is empty, creating new one" << std::endl;
                last_user_id = 10000; // Initialize default value for last_user_id
                saveUsers();
                return true;
            }

            // Read file into string
            std::string json_str(file_size, ' ');
            file.read(&json_str[0], file_size);
            file.close();

            std::cout << "[USER_MANAGER] Loading data from file, size: " << file_size << " bytes" << std::endl;
            // Debug output
            // std::cout << "[USER_MANAGER] JSON content: " << json_str << std::endl;

            // Parse JSON using simdjson
            auto json = json_parser.parse(json_str);

            // Load the last assigned user ID
            // Check if last_user_id field exists in JSON
            if (json["last_user_id"].error() != simdjson::error_code::SUCCESS) {
                std::cout << "[USER_MANAGER] Warning: last_user_id field not found, using default value" << std::endl;
                last_user_id = 10000;  // Default value
            }
            else {
                uint64_t loaded_id = 0;
                auto id_result = json["last_user_id"].get(loaded_id);
                if (id_result) {
                    std::cerr << "[USER_MANAGER] Error loading last_user_id: " << id_result << std::endl;
                    last_user_id = 10000;  // Default value on error
                }
                else {
                    last_user_id = static_cast<uint32_t>(loaded_id);
                    std::cout << "[USER_MANAGER] Successfully loaded last_user_id: " << last_user_id << std::endl;
                }
            }

            auto users_array = json["users"];

            if (!users_array.is_array()) {
                std::cerr << "[USER_MANAGER] Error: 'users' is not an array" << std::endl;
                return false;
            }

            // Clear existing users list
            users.clear();

            // Load users array from JSON
            for (simdjson::dom::element user_element : users_array) {
                User user;

                // Load user ID with error handling
                if (user_element["id"].error() != simdjson::error_code::SUCCESS) {
                    std::cerr << "[USER_MANAGER] Warning: user doesn't have id field" << std::endl;
                    user.id = 0; // Default ID
                }
                else {
                    uint64_t user_id = 0;
                    auto id_result = user_element["id"].get(user_id);
                    if (id_result) {
                        std::cerr << "[USER_MANAGER] Error getting user ID: " << id_result << std::endl;
                        user.id = 0;
                    }
                    else {
                        user.id = static_cast<uint32_t>(user_id);
                        std::cout << "[USER_MANAGER] Successfully loaded user ID: " << user.id << std::endl;

                        // Update last_user_id if current user ID is greater than stored last_user_id
                        if (user.id > last_user_id) {
                            last_user_id = user.id;
                            std::cout << "[USER_MANAGER] Updated last_user_id based on existing user: " << last_user_id << std::endl;
                        }
                    }
                }

                // Load other user fields
                std::string_view username_view;
                user_element["username"].get(username_view);
                user.username = std::string(username_view);

                std::string_view password_hash_view;
                user_element["password_hash"].get(password_hash_view);
                user.password_hash = std::string(password_hash_view);

                std::string_view email_view;
                user_element["email"].get(email_view);
                user.email = std::string(email_view);

                std::string_view registration_date_view;
                user_element["registration_date"].get(registration_date_view);
                user.registration_date = std::string(registration_date_view);

                // Debug output
                std::cout << "[USER_MANAGER] Loaded user: " << user.username
                    << ", ID=" << user.id << std::endl;

                users.push_back(user);
            }

            std::cout << "[USER_MANAGER] Total users loaded: " << users.size() << std::endl;
            std::cout << "[USER_MANAGER] Current last_user_id: " << last_user_id << std::endl;
            return true;
        }
        catch (const simdjson::simdjson_error& e) {
            std::cerr << "[USER_MANAGER] simdjson error loading users: " << e.what() << std::endl;
            return false;
        }
        catch (const std::exception& e) {
            std::cerr << "[USER_MANAGER] Error loading users: " << e.what() << std::endl;
            return false;
        }
    }

    // In user_manager.h, after saveUsers() method:
    // Modified saveUsers() method using generateJsonString()
   // In user_manager.h, in UserManager class
    bool saveUsers() {
        std::lock_guard<std::mutex> lock(mutex);

        try {
            // Generate JSON string using generateJsonString() method
            std::string json_str = generateJsonString();

            // Write to file with error handling
            std::ofstream file(database_file);
            if (!file.is_open()) {
                std::cerr << "[USER_MANAGER] Error: Could not open file for writing: " << database_file << std::endl;
                return false;
            }

            file << json_str;

            // Check if write operation was successful
            if (file.fail()) {
                std::cerr << "[USER_MANAGER] Error: Failed to write to file: " << database_file << std::endl;
                file.close();
                return false;
            }

            file.close();
            std::cout << "[USER_MANAGER] Successfully saved " << users.size() << " users and last_user_id=" << last_user_id << " to file" << std::endl;
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[USER_MANAGER] Error saving users: " << e.what() << std::endl;
            return false;
        }
    }

    // Modified registerUser method in UserManager class
    // To be added to user_manager.h, in UserManager class

 // In user_manager.h, in UserManager class
    bool registerUser(const User& user) {
        //std::lock_guard<std::mutex> lock(mutex);

        // Check if user with same username already exists
        for (const auto& existing_user : users) {
            if (existing_user.username == user.username) {
                std::cout << "[USER_MANAGER] Error: User with name " << user.username << " already exists!" << std::endl;
                return false; // User already exists
            }
        }

        // Create new user for registration
        User new_user = user;

        // Increment last_user_id and assign it to new user
        last_user_id++;
        new_user.id = last_user_id;

        std::cout << "[USER_MANAGER] Registering new user: " << new_user.username
            << " with ID=" << new_user.id << std::endl;

        // Add user to the list
        users.push_back(new_user);

        // Save users list to file
        bool saved = saveUsers();
        if (!saved) {
            std::cerr << "[USER_MANAGER] Failed to save users after registration!" << std::endl;
        }
        return saved;
    }
    
    // Authenticate user with username and password
    bool authenticateUser(const std::string& username, const std::string& password) {
        loadUsers();

        for (const auto& user : users) {
            if (user.username == username) {
                if (user.password_hash == hashPassword(password)) {
                    return true;
                }
                break;
            }
        }

        return false;
    }

    // Get username by client ID for online users
    std::string getUsernameByClientId(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(mutex);

        for (const auto& user : users) {
            if (user.is_online && user.client_id == clientId) {
                return user.username;
            }
        }

        return ""; // User not found
    }
};

// Get username by user ID
std::string UserManager::getUsernameByID(uint32_t userID) {
    std::lock_guard<std::mutex> lock(mutex);

    try {
        // Search for user in users list by ID
        for (const auto& user : users) {
            if (user.id == userID) {
                return user.username;
            }
        }

        // Return empty string if not found
        return "";

    }
    catch (const std::exception& e) {
        std::cerr << "[USER_MANAGER] Error in getUsernameByID: " << e.what() << std::endl;
        return "";
    }
}