#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define NOMINMAX
#include <winsock2.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
using namespace chrono;

struct CommandPacket
{
    uint32_t length;
    char     command[256];
};

struct MatrixUploadInfo 
{
    uint32_t matrix_size;
    uint32_t num_threads;
    uint32_t matrix_bytes;
};

int recveiveAll(SOCKET s, char* buffer, int length)
{
    int counter = 0;
    while (counter < length)
    {
        int n = recv(s, buffer + counter, length - counter, 0);
        if (n <= 0)
        {
            return -1;
        }
        counter += n;
    }
    return counter;
}

int sendAll(SOCKET s, const char* data, int length)
{
    int counter = 0;
    while (counter < length)
    {
        int n = send(s, data + counter, length - counter, 0);
        if (n <= 0)
        {
            return -1;
        }
        counter += n;
    }
    return counter;
}

bool sendCommand(SOCKET s, const string& cmd)
{
    if (cmd.size() > 256)
    {
        return false;
    }
    CommandPacket pkt{};
    pkt.length = htonl(static_cast<uint32_t>(cmd.size()));
    memcpy(pkt.command, cmd.data(), cmd.size());
    return sendAll(s, reinterpret_cast<char*>(&pkt), sizeof(pkt)) == sizeof(pkt);
}

bool receiveCommand(SOCKET s, string& outCmd)
{
    CommandPacket pkt{};
    if (recveiveAll(s, reinterpret_cast<char*>(&pkt), sizeof(pkt)) != sizeof(pkt))
    {
        return false;
    }
    uint32_t len = ntohl(pkt.length);
    if (len > 256)
    {
        return false;
    }
    outCmd.assign(pkt.command, pkt.command + len);
    return true;
}

int main()
{
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(12345);
    srv.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) != 0)
    {
        cerr << "Cannot connect to server\n";
        return 1;
    }

    sendCommand(sock, "HELLO");
    string reply;
    receiveCommand(sock, reply);
    cout << "[s] " << reply << "\n";

    cout << "Matrix size: ";
    int n; cin >> n;

    cout << "Enter thread counts (space‑separated, empty = default 1 2 4 8 16 32): ";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    string line; getline(cin, line);
    vector<int> cfg;
    istringstream iss(line);
    int t;
    while (iss >> t)
    {
        if (t > 0)
        {
            cfg.push_back(t);
        }
    }
    if (cfg.empty())
    {
        cfg = { 1, 2, 4, 8, 16, 32 };
    }
    vector<vector<int>> matrix(n, vector<int>(n));
    for (auto& row : matrix)
    {
        for (int& v : row)
        {
            v = rand() % 1000;
        }
    }
    sendCommand(sock, "UPLOAD_MATRIX");
    MatrixUploadInfo hdr{};
    hdr.matrix_size = htonl(n);
    hdr.num_threads = htonl(static_cast<uint32_t>(cfg.size()));
    hdr.matrix_bytes = htonl(n * n * sizeof(int));
    sendAll(sock, reinterpret_cast<char*>(&hdr), sizeof(hdr));

    vector<int> cfgNet(cfg.size());
    for (size_t i = 0; i < cfg.size(); ++i)
    {
        cfgNet[i] = htonl(cfg[i]);
    }
    sendAll(sock, reinterpret_cast<char*>(cfgNet.data()), cfgNet.size() * sizeof(int));

    vector<int> flat; 
    flat.reserve(n * n);
    for (auto& row : matrix)
    {
        flat.insert(flat.end(), row.begin(), row.end());
    }
    for (int& v : flat)
    {
        v = htonl(v);
    }
    sendAll(sock, reinterpret_cast<char*>(flat.data()), flat.size() * sizeof(int));

    receiveCommand(sock, reply);
    cout << "[s] " << reply << "\n";

    sendCommand(sock, "START_PROCESSING");

    atomic<bool> done(false);
    string finalResult;
    atomic<bool> resultReady = false;


    thread listener([&] 
        {
        string msg;
        while (receiveCommand(sock, msg))
        {
            if (msg.rfind("INFO:", 0) == 0 || msg.rfind("STATUS:", 0) == 0)
            {
                cout << "[s] " << msg << "\n";
            }
            else if (msg == "PROCESSING_COMPLETED")
            {
                cout << "[s] " << msg << "\n";
                done = true;
            }
            else if (msg.rfind("RESULT:", 0) == 0)
            {
                finalResult = msg;          
                resultReady = true;
                break;
            }
            else 
            {
                cout << "[s] " << msg << "\n";
            }
        }
        });

    cout << "Press <Enter> to request status…\n";
    cin.get();
    while (!done)
    {
        cin.get();
        sendCommand(sock, "REQUEST_STATUS");
    }

    sendCommand(sock, "REQUEST_RESULTS");
    while (!resultReady)
    {
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    listener.join();

    cout << "\n===== RESULTS =====\n";
    cout << finalResult << "\n";

    closesocket(sock);
    WSACleanup();
    return 0;
}
