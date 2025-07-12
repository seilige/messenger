#pragma once
#include "net_common.h"

namespace olc
{
    namespace net
    {
        // Forward declaration of connection class for use in owned_message
        template <typename T>
        class connection;

        // Message header structure containing message ID and size information
        template <typename T>
        struct messageHeader
        {
            T id{};                // Message type identifier
            uint32_t size = 0;     // Size of the message body in bytes
        };

        // Main message structure containing header and body data
        template <typename T>
        struct message
        {
            messageHeader<T> header{};      // Message header with ID and size
            std::vector<uint8_t> body;      // Message body containing raw data

            // Returns the total size of the message (header + body)
            size_t size() const
            {
                return sizeof(messageHeader<T>) + body.size();
            }

            // Stream output operator for displaying message information
            friend std::ostream& operator << (std::ostream& os, const message<T>& msg)
            {
                os << "ID: " << int(msg.header.id) << " Size: " << msg.header.size;
                return os;
            }

            // Stream input operator for adding data to message body
            template<typename DataType>
            friend message<T>& operator << (message<T>& msg, const DataType& data)
            {
                // Ensure data type is compatible with raw memory operations
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex");

                // Store current body size and resize to accommodate new data
                size_t i = msg.body.size();
                msg.body.resize(msg.body.size() + sizeof(DataType));
                
                // Copy data into message body using raw memory copy
                std::memcpy(msg.body.data() + i, &data, sizeof(DataType));
                
                // Update header size to reflect new message size
                msg.header.size = msg.size();

                return msg;
            }

            size_t readPos = 0;  // Current read position for sequential data extraction

            // Reset read position to beginning of message body
            void reset_read_position()
            {
                readPos = 0;
            }

            // Stream output operator for extracting data from message body
            template<typename DataType>
            friend message<T>& operator >> (message<T>& msg, DataType& data)
            {
                // Ensure data type is compatible with raw memory operations
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex");

                // Check if there's enough data remaining to read the requested type
                if (msg.readPos + sizeof(DataType) <= msg.body.size())
                {
                    // Copy data from message body to output variable
                    std::memcpy(&data, msg.body.data() + msg.readPos, sizeof(DataType));
                    msg.readPos += sizeof(DataType);
                }
                else
                {
                    // Handle case where requested data exceeds available message body
                    std::cerr << "Warning: Attempting to read beyond message body!" << std::endl;
                }

                return msg;
            }

        };

        // Owned message structure that associates a message with its source connection
        template <typename T>
        struct owned_message
        {
            std::shared_ptr<connection<T>> remote = nullptr;    // Pointer to the connection that sent this message
            message<T> msg;                                     // The actual message data

            // Stream output operator for displaying owned message information
            friend std::ostream& operator<<(std::ostream& os, const owned_message<T>& msg)
            {
                os << msg.msg;
                return os;
            }
        };
    }
}