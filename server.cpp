#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <limits>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
using namespace chrono;

struct CommandPacket 
{
    uint32_t length;
    char command[256];
};

struct MatrixUploadInfo 
{
    uint32_t matrix_size;
    uint32_t num_threads;
    uint32_t matrix_bytes;
};

struct ClientTask
{
    vector<vector<int>> matrix;
    vector<int> cfg;
    vector<double> time_res;
    size_t idx = 0;
    bool isProcessing = 0;
};

unordered_map<SOCKET, ClientTask> clients_list;

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

void computeRange(vector<vector<int>>& m, int startRow, int endRow) 
{
    int n = static_cast<int>(m.size());
    for (int i = startRow; i < endRow; ++i) 
    {
        int evenSum = 0;
        for (int j = 0; j < n; ++j)
        {
            if (j % 2 == 0)
            {
                evenSum += m[i][j];
            }        
        }
        m[i][i] = evenSum;
        this_thread::sleep_for(milliseconds(5));
    }
}

void computeMatrix(vector<vector<int>>& m, int threads) 
{
    if (threads <= 1) 
    {
        computeRange(m, 0, static_cast<int>(m.size()));
        return;
    }

    int n = static_cast<int>(m.size());
    int base = n / threads;
    int remainder = n % threads;
    int startRow = 0;
    vector<thread> pool;

    for (int t = 0; t < threads; ++t) 
    {
        int rows = base + (t < remainder ? 1 : 0);
        int endRow = startRow + rows;
        pool.emplace_back(computeRange, ref(m), startRow, endRow);
        startRow = endRow;
    }

    for (auto& th : pool)
    { 
        th.join();
    }
}

void serveClient(SOCKET cs) 
{
    ClientTask& d = clients_list[cs];

    try 
    {
        string cmd;
        while (receiveCommand(cs, cmd)) 
        {
            cerr << "[c " << cs << "] " << cmd << '\n';

            if (cmd == "HELLO") 
            {
                sendCommand(cs, "WELCOME");
            }
            else if (cmd == "UPLOAD_MATRIX") 
            {
                MatrixUploadInfo h{};
                if (recveiveAll(cs, reinterpret_cast<char*>(&h), sizeof(h)) != sizeof(h))
                { 
                    throw runtime_error("header error");
                }
                int n = ntohl(h.matrix_size);
                int cfgCnt = ntohl(h.num_threads);
                int bytes = ntohl(h.matrix_bytes);
                if (bytes != n * n * 4)
                { 
                    throw runtime_error("size mismatch");
                }
                d.cfg.resize(cfgCnt);
                if (recveiveAll(cs, reinterpret_cast<char*>(d.cfg.data()), cfgCnt * 4) != cfgCnt * 4)
                {
                    throw runtime_error("config read error");
                }
                for (int& v : d.cfg) 
                { 
                    v = ntohl(v);
                }
                vector<int> flat(n * n);
                if (recveiveAll(cs, reinterpret_cast<char*>(flat.data()), bytes) != bytes)
                { 
                    throw runtime_error("matrix data error");
                }
                d.matrix.assign(n, vector<int>(n));
                for (int i = 0; i < n; ++i)
                { 
                    for (int j = 0; j < n; ++j)
                    { 
                        d.matrix[i][j] = ntohl(flat[i * n + j]);
                    }
                }
                sendCommand(cs, "MATRIX_RECEIVED");
            }
            else if (cmd == "START_PROCESSING") 
            {
                if (d.matrix.empty()) 
                {
                    sendCommand(cs, "ERROR: NO DATA");
                    continue;
                }

                d.isProcessing = 1;
                d.time_res.clear();
                d.idx = 0;
                sendCommand(cs, "PROCESSING_STARTED");

                thread([cs]() 
                    {
                    ClientTask& ct = clients_list[cs];
                    for (size_t i = 0; i < ct.cfg.size(); ++i) 
                    {
                        ct.idx = i;
                        int thr = ct.cfg[i];

                        auto copy = ct.matrix;
                        auto t0 = high_resolution_clock::now();
                        computeMatrix(copy, thr);
                        double seconds = duration<double>(high_resolution_clock::now() - t0).count();

                        ct.time_res.push_back(seconds);
                        sendCommand(cs, "INFO: threads=" + to_string(thr) + ",time=" + to_string(seconds));

                    }
                    ct.isProcessing = 0;
                    sendCommand(cs, "PROCESSING_COMPLETED");
                    }).detach();

            }
            else if (cmd == "REQUEST_STATUS") 
            {
                if (!d.isProcessing)
                {
                    sendCommand(cs, "status — FINISHED");
                }
                else
                { 
                    sendCommand(cs, "status — " + to_string(d.idx + 1) + "/" + to_string(d.cfg.size()));
                }
            }
            else if (cmd == "REQUEST_RESULTS") 
            {
                string report = "RESULT:\nMatrix " + to_string(d.matrix.size()) + "x" + to_string(d.matrix.size());
                for (size_t i = 0; i < d.cfg.size(); ++i)
                { 
                    report += "\n" + to_string(d.cfg[i]) + " threads: " + to_string(d.time_res[i]) + " s";
                }
                sendCommand(cs, report);
            }
        }
    }
    catch (const exception& e) 
    {
        cerr << "[s] exception: " << e.what() << '\n';
    }

    closesocket(cs);
    clients_list.erase(cs);
}

int main() 
{

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) 
    {
        cerr << "[ERROR] WSAStartup failed\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET)
    {
        cerr << "[ERROR] socket() failed\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) 
    {
        cerr << "[ERROR] bind() failed\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }
  
    listen(serverSocket, SOMAXCONN);
    cerr << "[s] Listening on port 12345\n";

    while (true) 
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        thread(serveClient, clientSocket).detach();
    }
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
