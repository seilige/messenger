#pragma once
#include "net_common.h"

namespace olc
{
    namespace net
    {
        // Forward declaration of connection class for use in owned_message
        template <typename T>
        class connection;

        // Message header structure containing message ID and size
        template <typename T>
        struct messageHeader
        {
            T id{};
            uint32_t size = 0;
        };

        // Main message structure containing header and body data
        template <typename T>
        struct message
        {
            messageHeader<T> header{};
            std::vector<uint8_t> body;

            // Returns total message size (header + body)
            size_t size() const
            {
                return sizeof(messageHeader<T>) + body.size();
            }

            // Stream output operator for message display
            friend std::ostream& operator << (std::ostream& os, const message<T>& msg)
            {
                os << "ID: " << int(msg.header.id) << " Size: " << msg.header.size;
                return os;
            }

            // Stream input operator for adding data to message body
            template<typename DataType>
            friend message<T>& operator << (message<T>& msg, const DataType& data)
            {
                // Check that data type is trivially copyable
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex");

                size_t i = msg.body.size();
                msg.body.resize(msg.body.size() + sizeof(DataType));
                std::memcpy(msg.body.data() + i, &data, sizeof(DataType));
                msg.header.size = msg.size();

                return msg;
            }

            size_t readPos = 0;  // Current read position in message body

            // Reset read position to beginning of message body
            void reset_read_position()
            {
                readPos = 0;
            }

            // Stream output operator for extracting data from message body
            template<typename DataType>
            friend message<T>& operator >> (message<T>& msg, DataType& data)
            {
                // Check that data type is trivially copyable
                static_assert(std::is_standard_layout<DataType>::value, "Data is too complex");

                // Check if we have enough data to read without going beyond buffer
                if (msg.readPos + sizeof(DataType) <= msg.body.size())
                {
                    // Read data from current position
                    std::memcpy(&data, msg.body.data() + msg.readPos, sizeof(DataType));
                    msg.readPos += sizeof(DataType);
                }
                else
                {
                    // Handle case when trying to read beyond available data
                    std::cerr << "Warning: Attempting to read beyond message body!" << std::endl;
                }

                return msg;
            }

        };

        // Owned message structure for identifying the source connection of a message
        template <typename T>
        struct owned_message
        {
            std::shared_ptr<connection<T>> remote = nullptr;
            message<T> msg;

            // Stream output operator for owned message display
            friend std::ostream& operator<<(std::ostream& os, const owned_message<T>& msg)
            {
                os << msg.msg;
                return os;
            }
        };
    }
}