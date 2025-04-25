#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define ROOT "webroot"

using namespace std;

string readAll(const string& path) 
{
    ifstream in(path, ios::binary);
    if (!in) return {};
    ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void reply(SOCKET s, int code, const string& body) 
{
    string status = (code == 200 ? "200 OK" :
        code == 404 ? "404 Not Found" :
        code == 405 ? "405 Method Not Allowed" :
        "500 Internal Server Error");
    ostringstream hdr;
    hdr << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n";
    string out = hdr.str() + body;
    send(s, out.c_str(), (int)out.size(), 0);
}

void session(SOCKET client) 
{
    char buf[4096];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) 
    { 
        closesocket(client); return; 
    }
    buf[n] = '\0';

    string req(buf);
    if (req.rfind("GET ", 0) != 0) 
    {
        reply(client, 405, "<h1>405 Method Not Allowed</h1>");
        closesocket(client);
        return;
    }
    size_t pos = req.find(' ', 4);
    string uri = req.substr(4, pos - 4);

    if (uri == "/") uri = "/index.html";

    string path = string(ROOT) + uri;
    string body = readAll(path);

    if (body.empty()) {
        reply(client, 404, "<h1>404 Not Found</h1>");
    }
    else {
        reply(client, 200, body);
    }

    closesocket(client);
}

int main() 
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cerr << "WSAStartup failed\n"; return 1;
    }

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) { WSACleanup(); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR || listen(srv, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "bind/listen failed\n";
        closesocket(srv);
        WSACleanup();
        return 1;
    }

    cout << "Listening on http://localhost:" << PORT << "\n";

    while (true) {
        SOCKET cli = accept(srv, nullptr, nullptr);
        if (cli == INVALID_SOCKET) continue;
        thread(session, cli).detach();
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}
