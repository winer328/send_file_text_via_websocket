#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <conio.h>  // For _kbhit()
#include <fstream>  // For file operations
#include <vector>   // For byte vectors
#include <sstream>  // For string streams

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

class WebSocketClient {
private:
    SOCKET socket_fd;
    bool connected;

    // Generate a simple WebSocket key (simplified for clarity)
    std::string generateKey() {
        // In a real implementation, this would be proper base64-encoded random bytes
        // For simplicity, we're using a hardcoded key that's valid for the handshake
        return "dGhlIHNhbXBsZSBub25jZQ==";
    }

    // Create WebSocket frame for a text message
    std::string createFrame(const std::string& message, bool isText = true) {
        std::string frame;
        uint8_t byte1 = isText ? 0x81 : 0x82; // Text (0x81) or Binary (0x82) frame, FIN bit set
        frame.push_back(byte1);

        // Set length and mask bit
        size_t length = message.length();
        if (length <= 125) {
            uint8_t byte2 = 0x80 | length; // Mask bit set with length
            frame.push_back(byte2);
        }
        else if (length <= 65535) {
            uint8_t byte2 = 0x80 | 126; // Mask bit set with 16-bit length
            frame.push_back(byte2);
            frame.push_back((length >> 8) & 0xFF);
            frame.push_back(length & 0xFF);
        }
        else {
            uint8_t byte2 = 0x80 | 127; // Mask bit set with 127 for 64-bit length
            frame.push_back(byte2);

            // 64-bit length (most significant byte first)
            for (int i = 7; i >= 0; i--) {
                frame.push_back((length >> (i * 8)) & 0xFF);
            }
        }

        // Add masking key (4 random bytes)
        uint8_t mask[4];
        for (int i = 0; i < 4; i++) {
            mask[i] = rand() % 256;
            frame.push_back(mask[i]);
        }

        // Add masked data
        for (size_t i = 0; i < message.length(); i++) {
            frame.push_back(message[i] ^ mask[i % 4]);
        }

        return frame;
    }

    // Create WebSocket frame for binary data
    std::string createBinaryFrame(const std::vector<uint8_t>& data) {
        std::string message(data.begin(), data.end());
        return createFrame(message, false); // false means binary frame (opcode 0x02)
    }

    // Parse received WebSocket frame (simplified version)
    std::string parseFrame(const std::string& frame) {
        if (frame.empty()) return "";

        size_t idx = 0;
        uint8_t firstByte = frame[idx++];
        uint8_t secondByte = frame[idx++];

        // Check for text frame
        bool fin = (firstByte & 0x80) != 0;
        uint8_t opcode = firstByte & 0x0F;
        if (opcode != 0x01 && opcode != 0x02) return ""; // Only handling text and binary frames

        // Get payload length
        uint8_t masked = (secondByte & 0x80) != 0;
        uint64_t payloadLength = secondByte & 0x7F;

        if (payloadLength == 126) {
            payloadLength = (frame[idx++] << 8) | frame[idx++];
        }
        else if (payloadLength == 127) {
            payloadLength = 0;
            for (int i = 0; i < 8; i++) {
                payloadLength = (payloadLength << 8) | frame[idx++];
            }
        }

        // Handle masking
        uint8_t mask[4] = { 0 };
        if (masked) {
            for (int i = 0; i < 4; i++) {
                mask[i] = frame[idx++];
            }
        }

        // Extract and unmask payload
        std::string payload;
        for (size_t i = 0; i < payloadLength; i++) {
            if (idx + i < frame.length()) {
                uint8_t byte = frame[idx + i];
                if (masked) {
                    byte ^= mask[i % 4];
                }
                payload.push_back(byte);
            }
        }

        return payload;
    }

public:
    WebSocketClient() : socket_fd(INVALID_SOCKET), connected(false) {
        srand((unsigned int)time(NULL));

        // Initialize Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << std::endl;
        }
    }

    ~WebSocketClient() {
        disconnect();
        WSACleanup();
    }

    bool connect(const std::string& host, int port, const std::string& path) {
        // Resolve the server address and port
        struct addrinfo hints;
        struct addrinfo* result = NULL;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Convert port to string
        char portStr[10];
        sprintf_s(portStr, sizeof(portStr), "%d", port);

        int iResult = getaddrinfo(host.c_str(), portStr, &hints, &result);
        if (iResult != 0) {
            std::cerr << "getaddrinfo failed: " << iResult << std::endl;
            return false;
        }

        // Create socket
        socket_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (socket_fd == INVALID_SOCKET) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            return false;
        }

        // Connect to server
        iResult = ::connect(socket_fd, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result); // Done with this structure

        if (iResult == SOCKET_ERROR) {
            std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
            closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
            return false;
        }

        std::cout << "TCP connection established to " << host << ":" << port << std::endl;

        // Perform WebSocket handshake
        std::string key = generateKey();
        std::string handshake =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

        iResult = send(socket_fd, handshake.c_str(), (int)handshake.length(), 0);
        if (iResult == SOCKET_ERROR) {
            std::cerr << "Failed to send handshake: " << WSAGetLastError() << std::endl;
            closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
            return false;
        }

        // Receive handshake response
        char buffer[4096] = { 0 };
        iResult = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (iResult <= 0) {
            std::cerr << "Server did not respond to handshake" << std::endl;
            closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
            return false;
        }

        std::string response(buffer);
        if (response.find("HTTP/1.1 101") == std::string::npos) {
            std::cerr << "WebSocket handshake failed" << std::endl;
            closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
            return false;
        }

        connected = true;
        std::cout << "WebSocket connection established" << std::endl;
        return true;
    }

    void disconnect() {
        if (socket_fd != INVALID_SOCKET) {
            closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
        }
        connected = false;
    }

    bool sendMessage(const std::string& message) {
        if (!connected) return false;

        std::string frame = createFrame(message, true);  // true for text frame
        int bytes = send(socket_fd, frame.c_str(), (int)frame.length(), 0);
        return bytes != SOCKET_ERROR;
    }

    // Send a file with URL format
    bool sendFile(const std::string& filePath, const std::string& url) {
        if (!connected) return false;

        // Open the file
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return false;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read file content
        std::vector<uint8_t> fileData(fileSize);
        file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        file.close();

        // First send a JSON message with file metadata
        std::string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);
        std::ostringstream metadataJson;
        metadataJson << "{"
            << "\"type\":\"file\","
            << "\"url\":\"" << url << "\","
            << "\"fileName\":\"" << fileName << "\","
            << "\"fileSize\":" << fileSize
            << "}";

        if (!sendMessage(metadataJson.str())) {
            std::cerr << "Failed to send file metadata" << std::endl;
            return false;
        }

        // Then send the actual file content in binary format
        std::string frame = createBinaryFrame(fileData);
        int bytes = send(socket_fd, frame.c_str(), (int)frame.length(), 0);

        if (bytes == SOCKET_ERROR) {
            std::cerr << "Failed to send file content: " << WSAGetLastError() << std::endl;
            return false;
        }

        std::cout << "Sent file: " << fileName << " (" << fileSize << " bytes) as URL: " << url << std::endl;
        return true;
    }

    std::string receiveMessage() {
        if (!connected) return "";

        char buffer[4096] = { 0 };
        int bytes = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            connected = false;
            return "";
        }

        return parseFrame(std::string(buffer, bytes));
    }

    bool isConnected() const {
        return connected;
    }

    // Check if data is available to read with timeout (ms)
    bool dataAvailable(int timeout_ms) {
        if (!connected) return false;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socket_fd, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        // On Windows, the first parameter is ignored, but we still follow convention
        return select(0, &readfds, NULL, NULL, &tv) > 0;
    }
};

int main(int argc, char* argv[]) {
    // Default values
    std::string host = "172.86.105.168";
    int port = 8080;
    std::string path = "/";

    // Override defaults if command-line arguments are provided
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);
    if (argc >= 4) path = argv[3];

    WebSocketClient client;

    std::cout << "Connecting to " << host << ":" << port << path << "..." << std::endl;
    if (!client.connect(host, port, path)) {
        std::cerr << "Failed to connect" << std::endl;
        std::cout << "Press any key to exit..." << std::endl;
        _getch(); // Wait for a key press before exiting
        return 1;
    }

    std::cout << "Connected! Commands: \n"
        << "  'file:<filepath>:<url>' - Send a file with URL\n"
        << "  [any text] - Send text message\n"
        << "  [empty line] - Exit\n" << std::endl;

    // Main message loop
    bool running = true;
    while (running && client.isConnected()) {
        // Check for incoming messages
        if (client.dataAvailable(100)) {
            std::string message = client.receiveMessage();
            if (!message.empty()) {
                std::cout << "Received: " << message << std::endl;
            }
            else if (message == "") {
                std::cout << "Connection closed by server" << std::endl;
                running = false;
            }
            else if (!client.isConnected()) {
                std::cout << "Connection closed by server" << std::endl;
                break;
            }
        }

        // Check for user input - non-blocking approach for Windows
        if (_kbhit()) {
            std::string input;
            std::getline(std::cin, input);

            if (input.empty()) {
                running = false;
            }
            else if (input.substr(0, 5) == "file:") {
                // Parse file command: file:<filepath>:<url>
                size_t firstColon = input.find(':');
                size_t secondColon = input.find(':', firstColon + 1);

                if (secondColon != std::string::npos) {
                    std::string filePath = input.substr(firstColon + 1, secondColon - firstColon - 1);
                    std::string url = input.substr(secondColon + 1);

                    std::cout << "Sending file: " << filePath << " as URL: " << url << std::endl;
                    if (!client.sendFile(filePath, url)) {
                        std::cerr << "Failed to send file" << std::endl;
                    }
                }
                else {
                    std::cout << "Invalid file command format. Use: file:<filepath>:<url>" << std::endl;
                }
            }
            else {
                std::cout << "Sending text: " << input << std::endl;
                client.sendMessage(input);
            }
        }

        // Sleep a bit to prevent high CPU usage
        Sleep(10);
    }

    std::cout << "Disconnected" << std::endl;
    std::cout << "Press any key to exit..." << std::endl;
    _getch(); // Wait for a key press before exiting
    return 0;
}