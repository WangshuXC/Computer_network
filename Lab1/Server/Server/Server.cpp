#include <iostream>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <ctime>

#pragma comment(lib, "ws2_32.lib") // socket库

using namespace std;

//#define ServIp "127.0.0.1"              //服务器ip，此处设置为回环地址
#define PORT 6262                       // 端口号
#define msgSize 1024                    // 缓冲区大小
#define MaxClient 5                     // 最大连接数
#define _CRT_SECURE_NO_WARNINGS         // 禁止使用不安全的函数报错
#define _WINSOCK_DEPRECATED_NO_WARNINGS // 禁止使用旧版本的函数报错

// 字体效果
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"

#define ANSI_UNDERLINE   "\x1b[4m"
#define ANSI_RESET   "\x1b[0m"

SOCKET Clients[MaxClient];    // 客户端socket数组
SOCKET Server;                // 服务器端socket
SOCKADDR_IN clientAddrs[MaxClient]; // 客户端地址数组
SOCKADDR_IN serverAddr;             // 定义服务器地址

int currentConnections = 0; // 当前连接的客户数
int connectCondition[MaxClient] = {}; // 每一个连接的情况

int isEmpty() // 查询空闲的连接口的索引
{
    for (int i = 0; i < MaxClient; i++)
    {
        if (connectCondition[i] == 0) // 连接空闲
        {
            return i;
        }
    }
    exit(EXIT_FAILURE);
}

DWORD WINAPI ThreadFunction(LPVOID lpParameter) // 线程函数
{
    int receByt = 0;
    char recvMsg[msgSize]; // 接收缓冲区
    char sendMsg[msgSize]; // 发送缓冲区

    int num = (int)lpParameter; // 当前连接的索引

    // 发送连接成功的提示消息
    snprintf(sendMsg, sizeof(sendMsg), "你的id是 \x1b[32m%d\x1b[0m\n", Clients[num]);
    send(Clients[num], sendMsg, strlen(sendMsg), 0);

    // 循环接收信息
    while (true)
    {
        Sleep(100); // 延时100ms
        receByt = recv(Clients[num], recvMsg, sizeof(recvMsg), 0); // 接收信息

        // 获取客户端ip地址
        char clientIp[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &(clientAddrs[num].sin_addr), clientIp, INET_ADDRSTRLEN);
        if (receByt > 0) // 接收成功
        {
            // 创建时间戳，记录当前通讯时间
            auto currentTime = chrono::system_clock::now();
            time_t timestamp = chrono::system_clock::to_time_t(currentTime);
            tm localTime;
            localtime_s(&localTime, &timestamp);
            char timeStr[50];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime); // 格式化时间

            cout << "Client [" ANSI_COLOR_RED << Clients[num] << ANSI_RESET << " " ANSI_COLOR_YELLOW << clientIp << ANSI_RESET "] 在 " ANSI_UNDERLINE << timeStr << ANSI_RESET " 发送：" ANSI_COLOR_YELLOW << endl << recvMsg << ANSI_RESET << endl << endl;
            sprintf_s(sendMsg, sizeof(sendMsg), "Id[" ANSI_COLOR_CYAN "%d" ANSI_RESET "] 在 " ANSI_UNDERLINE "%s" ANSI_RESET " 发送： \n" ANSI_COLOR_YELLOW "%s" ANSI_RESET "\n ", Clients[num], timeStr, recvMsg); // 格式化发送信息
            for (int i = 0; i < MaxClient; i++) // 将消息同步到所有聊天窗口
            {
                if (connectCondition[i] == 1 && i != num)
                {
                    send(Clients[i], sendMsg, strlen(sendMsg), 0); // 发送信息
                }
            }
        }
        else // 接收失败
        {
            if (WSAGetLastError() == 10054) // 客户端主动关闭连接
            {
                // 创建时间戳，记录当前通讯时间
                auto currentTime = chrono::system_clock::now();
                time_t timestamp = chrono::system_clock::to_time_t(currentTime);
                tm localTime;
                localtime_s(&localTime, &timestamp);
                char timeStr[50];
                strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime); // 格式化时间

                cout << "Client [" ANSI_COLOR_RED << Clients[num] << ANSI_RESET " " ANSI_COLOR_YELLOW << clientIp << ANSI_RESET "] 退出于 " ANSI_UNDERLINE << timeStr << ANSI_RESET << endl << endl;

                closesocket(Clients[num]);
                currentConnections--;
                connectCondition[num] = 0;
                cout << "当前客户端连接数量为: " ANSI_COLOR_CYAN << currentConnections << ANSI_RESET << endl << endl;
                return 0;
            }
            else
            {
                cout << "接收失败, Error:" << WSAGetLastError() << endl << endl;
                break;
            }
        }
    }
}

int main()
{
    // 初始化WinSock库
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 1), &wsaData) == -1) {
        cout << "初始化 WinSock 出错,Error:" << WSAGetLastError;
    }
    else {
        cout << "初始化 WinSock 成功" << endl;
    }

    //获取本机ip地址
    char hostName[256];
    if (gethostname(hostName, sizeof(hostName)) == SOCKET_ERROR) {
        cout << "gethostname 失败" << endl;
        exit(EXIT_FAILURE);
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    struct addrinfo* addrInfo = nullptr;
    if (getaddrinfo(hostName, nullptr, &hints, &addrInfo) != 0) {
        cout << "getaddrinfo 失败" << endl;
        exit(EXIT_FAILURE);
    }

    //存储本机ip
    char ipAddr[INET_ADDRSTRLEN];

    for (struct addrinfo* p = addrInfo; p != nullptr; p = p->ai_next) {
        struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);

        if (inet_ntop(AF_INET, &(ipv4->sin_addr), ipAddr, INET_ADDRSTRLEN) == nullptr) {
            cout << "inet_ntop 失败" << endl;
            exit(EXIT_FAILURE);
            continue;
        }
        else {
            cout << "本机IP地址 " ANSI_COLOR_MAGENTA << ipAddr << ANSI_RESET << endl;
        }
    }
    freeaddrinfo(addrInfo);


    Server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    /*
    AF_INET：使用ipv4（AF是指定地址族的宏，Address Family），也可以使用PF_INET,在使用Socket API进行套接字编程时二者是等价的
    SOCK_STREAM：套接字类型，保证了数据的有序性，确保了数据的完整性和可靠性，以及提供了流式传输的特性，确保传输的数据按照发送顺序被接收
                 同时还是实验要求的一部分
    IPPROTO_TCP：使用TCP传输协议
    */

    if (Server == INVALID_SOCKET) // 错误处理
    {
        perror("创建 Socket 错误");
        exit(EXIT_FAILURE);
    }
    cout << "创建 Socket 成功" << endl;

    // 绑定服务器地址
    serverAddr.sin_family = AF_INET;   // 地址类型
    serverAddr.sin_port = htons(PORT); // 端口号

    if (inet_pton(AF_INET, ipAddr, &(serverAddr.sin_addr)) != 1)//将ServIp转换为二进制并且存储进servAddr.sin_addr
    {
        cout << "服务端地址绑定出错" << endl;
        exit(EXIT_FAILURE);
    }
    else {
        cout << "服务端地址 " ANSI_COLOR_MAGENTA << ipAddr << ANSI_RESET << " 绑定成功" << endl;
    }
    if (bind(Server, (LPSOCKADDR)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) // 将服务器套接字与服务器地址和端口绑定
    {
        perror("套接字与端口绑定失败");
        exit(EXIT_FAILURE);
    }
    else
    {
        cout << "套接字与端口 " ANSI_COLOR_MAGENTA << PORT << ANSI_RESET " 绑定成功" << endl;
    }

    // 设置监听/等待队列
    if (listen(Server, MaxClient) != 0)
    {
        perror("设置监听失败");
        exit(EXIT_FAILURE);
    }
    else
    {
        cout << "设置监听成功" << endl;
    }

    cout << "服务端成功启动\n" << endl;

    // 循环接收客户端请求
    while (true)
    {
        if (currentConnections < MaxClient)
        {
            int num = isEmpty();
            int addrlen = sizeof(SOCKADDR);
            Clients[num] = accept(Server, (sockaddr*)&clientAddrs[num], &addrlen); // 等待客户端请求

            // 获取客户端ip地址
            char clientIp[INET_ADDRSTRLEN] = "";
            inet_ntop(AF_INET, &(clientAddrs[num].sin_addr), clientIp, INET_ADDRSTRLEN);

            if (Clients[num] == SOCKET_ERROR)
            {
                perror("客户端出错 \n");
                closesocket(Server);
                WSACleanup();
                exit(EXIT_FAILURE);
            }
            connectCondition[num] = 1;// 连接位置1表示占用
            currentConnections++; // 当前连接数加1

            // 创建时间戳，记录当前通讯时间
            auto currentTime = chrono::system_clock::now();
            time_t timestamp = chrono::system_clock::to_time_t(currentTime);
            tm localTime;
            localtime_s(&localTime, &timestamp);
            char timeStr[50];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime); // 格式化时间

            cout << "Client [" ANSI_COLOR_RED << Clients[num] << ANSI_RESET " " ANSI_COLOR_YELLOW << clientIp << ANSI_RESET "] 连接于 " ANSI_UNDERLINE << timeStr << ANSI_RESET << endl << endl;
            cout << "当前客户端连接数量为: " ANSI_COLOR_CYAN << currentConnections << ANSI_RESET << endl << endl;

            HANDLE Thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadFunction, (LPVOID)num, 0, NULL); // 创建线程
            if (Thread == NULL) // 线程创建失败
            {
                perror("线程创建失败\n");
                exit(EXIT_FAILURE);
            }
            else
            {
                CloseHandle(Thread);
            }
        }
        else
        {
            cout << "客户端数量已满" << endl << endl;
        }
    }

    closesocket(Server);
    WSACleanup();

    return 0;
}