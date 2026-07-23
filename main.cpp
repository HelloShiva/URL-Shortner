#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

using namespace std;

class UrlShortener {
public:
    explicit UrlShortener(const string& baseUrl) : baseUrl(baseUrl) {
        InitializeCriticalSection(&criticalSection);
    }

    ~UrlShortener() {
        DeleteCriticalSection(&criticalSection);
    }

    string shortenUrl(const string& longUrl) {
        EnterCriticalSection(&criticalSection);

        auto existing = urlToCode.find(longUrl);
        if (existing != urlToCode.end()) {
            string code = existing->second;
            LeaveCriticalSection(&criticalSection);
            return code;
        }

        string shortCode = generateUniqueCode(longUrl);
        codeToUrl[shortCode] = longUrl;
        urlToCode[longUrl] = shortCode;
        history.push_back({baseUrl + shortCode, longUrl});
        LeaveCriticalSection(&criticalSection);
        return shortCode;
    }

    string expandUrl(const string& shortCode) const {
        EnterCriticalSection(const_cast<LPCRITICAL_SECTION>(&criticalSection));

        auto it = codeToUrl.find(shortCode);
        if (it == codeToUrl.end()) {
            LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&criticalSection));
            return "";
        }
        string url = it->second;
        LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&criticalSection));
        return url;
    }

    void displayMappings() const {
        EnterCriticalSection(const_cast<LPCRITICAL_SECTION>(&criticalSection));

        if (history.empty()) {
            LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&criticalSection));
            cout << "\nNo URL mappings stored yet.\n";
            return;
        }

        cout << "\nStored URL Mappings\n";
        cout << left << setw(35) << "Short URL" << "Original URL\n";
        cout << string(100, '-') << '\n';

        for (const auto& entry : history) {
            cout << left << setw(35) << entry.shortUrl << entry.longUrl << '\n';
        }

        LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&criticalSection));
    }

private:
    struct UrlEntry {
        string shortUrl;
        string longUrl;
    };

    mutable CRITICAL_SECTION criticalSection;
    string baseUrl;
    unordered_map<string, string> codeToUrl;
    unordered_map<string, string> urlToCode;
    vector<UrlEntry> history;

    string generateUniqueCode(const string& longUrl) const {
        hash<string> hasher;
        size_t seed = hasher(longUrl);
        size_t attempt = 0;
        string code;

        do {
            size_t mixed = seed ^ (static_cast<size_t>(time(nullptr)) + 0x9e3779b97f4a7c15ULL +
                                        (attempt << 6) + (attempt >> 2));
            code = base62Encode(mixed).substr(0, 7);
            if (code.length() < 7) {
                code.append(7 - code.length(), '0');
            }
            ++attempt;
        } while (codeToUrl.find(code) != codeToUrl.end());

        return code;
    }

    static string base62Encode(size_t value) {
        const string alphabet = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

        if (value == 0) {
            return "0";
        }

        string encoded;
        while (value > 0) {
            encoded.push_back(alphabet[value % alphabet.size()]);
            value /= alphabet.size();
        }

        return string(encoded.rbegin(), encoded.rend());
    }
};

class HttpRedirectServer {
public:
    HttpRedirectServer(UrlShortener& shortener, int port)
        : shortener(shortener), port(port), running(false), listenSocket(INVALID_SOCKET), serverThread(nullptr) {}

    bool start() {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }

        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }

        sockaddr_in service{};
        service.sin_family = AF_INET;
        service.sin_addr.s_addr = inet_addr("127.0.0.1");
        service.sin_port = htons(static_cast<u_short>(port));

        if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR) {
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        running = true;
        serverThread = CreateThread(nullptr, 0, &HttpRedirectServer::threadEntry, this, 0, nullptr);
        if (serverThread == nullptr) {
            running = false;
            closesocket(listenSocket);
            listenSocket = INVALID_SOCKET;
            WSACleanup();
            return false;
        }
        return true;
    }

    void stop() {
        if (!running) {
            return;
        }

        running = false;
        shutdown(listenSocket, SD_BOTH);
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;

        if (serverThread != nullptr) {
            WaitForSingleObject(serverThread, INFINITE);
            CloseHandle(serverThread);
            serverThread = nullptr;
        }

        WSACleanup();
    }

    ~HttpRedirectServer() {
        stop();
    }

    int getPort() const {
        return port;
    }

private:
    UrlShortener& shortener;
    int port;
    volatile bool running;
    SOCKET listenSocket;
    HANDLE serverThread;

    static DWORD WINAPI threadEntry(LPVOID param) {
        HttpRedirectServer* server = static_cast<HttpRedirectServer*>(param);
        server->acceptLoop();
        return 0;
    }

    void acceptLoop() {
        while (running) {
            SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
            if (clientSocket == INVALID_SOCKET) {
                if (running) {
                    continue;
                }
                break;
            }

            handleClient(clientSocket);
            closesocket(clientSocket);
        }
    }

    void handleClient(SOCKET clientSocket) {
        char buffer[2048] = {};
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            return;
        }

        std::string request(buffer, bytesReceived);
        std::string path = extractPath(request);
        std::string code = normalizeCode(path);

        if (code.empty()) {
            sendTextResponse(clientSocket, "200 OK",
                             "<html><body><h2>URL Shortener Server Running</h2>"
                             "<p>Paste a generated short URL in the browser.</p></body></html>");
            return;
        }

        string originalUrl = shortener.expandUrl(code);
        if (originalUrl.empty()) {
            sendTextResponse(clientSocket, "404 Not Found",
                             "<html><body><h2>Short URL not found</h2></body></html>");
            return;
        }

        ostringstream response;
        response << "HTTP/1.1 302 Found\r\n";
        response << "Location: " << originalUrl << "\r\n";
        response << "Content-Length: 0\r\n";
        response << "Connection: close\r\n\r\n";
        send(clientSocket, response.str().c_str(), static_cast<int>(response.str().size()), 0);
    }

    static string extractPath(const string& request) {
        size_t methodEnd = request.find(' ');
        if (methodEnd == string::npos) {
            return "";
        }

        size_t pathEnd = request.find(' ', methodEnd + 1);
        if (pathEnd == string::npos) {
            return "";
        }

        return request.substr(methodEnd + 1, pathEnd - methodEnd - 1);
    }

    static string normalizeCode(const string& path) {
        if (path.empty() || path == "/") {
            return "";
        }

        string code = path[0] == '/' ? path.substr(1) : path;
        size_t queryPos = code.find('?');
        if (queryPos != string::npos) {
            code = code.substr(0, queryPos);
        }

        return code;
    }

    static void sendTextResponse(SOCKET clientSocket, const string& status, const string& body) {
        ostringstream response;
        response << "HTTP/1.1 " << status << "\r\n";
        response << "Content-Type: text/html; charset=UTF-8\r\n";
        response << "Content-Length: " << body.size() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << body;

        string message = response.str();
        send(clientSocket, message.c_str(), static_cast<int>(message.size()), 0);
    }
};

void displayMenu() {
    cout << "\nURL Shortener Backend Simulator\n";
    cout << "1. Shorten a long URL\n";
    cout << "2. Retrieve original URL using short code\n";
    cout << "3. Display all mappings\n";
    cout << "4. Exit\n";
    cout << "Enter your choice: ";
}

int main() {
    string baseUrl;
    UrlShortener* shortener = nullptr;
    HttpRedirectServer* server = nullptr;
    int choice = 0;

    for (int port = 8080; port <= 8090; ++port) {
        string candidateBaseUrl = "http://localhost:" + to_string(port) + "/";
        UrlShortener* candidateShortener = new UrlShortener(candidateBaseUrl);
        HttpRedirectServer* candidateServer = new HttpRedirectServer(*candidateShortener, port);

        if (candidateServer->start()) {
            baseUrl = candidateBaseUrl;
            shortener = candidateShortener;
            server = candidateServer;
            break;
        }

        delete candidateServer;
        delete candidateShortener;
    }

    if (server == nullptr || shortener == nullptr) {
        cout << "Failed to start local redirect server on ports 8080 to 8090.\n";
        cout << "Close any app using these ports and try again.\n";
        return 1;
    }

    cout << "Welcome to the URL Shortener Backend Simulator\n";
    cout << "Local redirect server is running at " << baseUrl << '\n';

    while (true) {
        displayMenu();

        if (!(cin >> choice)) {
            cout << "\nInvalid input. Please enter a numeric choice.\n";
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }

        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        if (choice == 1) {
            string longUrl;
            cout << "Enter the long URL: ";
            getline(cin, longUrl);

            if (longUrl.empty()) {
                cout << "URL cannot be empty.\n";
                continue;
            }

            string shortCode = shortener->shortenUrl(longUrl);
            cout << "Short code generated: " << shortCode << '\n';
            cout << "Short URL: " << baseUrl << shortCode << '\n';
            cout << "Original URL: " << longUrl << '\n';
        } else if (choice == 2) {
            string shortCode;
            cout << "Enter the short code: ";
            getline(cin, shortCode);

            string originalUrl = shortener->expandUrl(shortCode);
            if (originalUrl.empty()) {
                cout << "No URL found for short code: " << shortCode << '\n';
            } else {
                cout << "Short URL: " << baseUrl << shortCode << '\n';
                cout << "Original URL: " << originalUrl << '\n';
            }
        } else if (choice == 3) {
            shortener->displayMappings();
        } else if (choice == 4) {
            cout << "Exiting URL Shortener Backend Simulator.\n";
            break;
        } else {
            cout << "Please choose an option from 1 to 4.\n";
        }
    }

    server->stop();
    delete server;
    delete shortener;
    return 0;
}
