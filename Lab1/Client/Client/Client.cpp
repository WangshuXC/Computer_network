#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <cstring>

#pragma comment(lib,"ws2_32.lib")   //socket库
using namespace std;

#define ServIp "10.130.101.102" //默认服务端IP
#define PORT 6262  //端口号
#define msgSize 1024  //缓冲区大小

SOCKET Client; //定义客户端socket
SOCKADDR_IN servAddr; //定义服务器地址

// 字体效果
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"

#define ANSI_UNDERLINE   "\x1b[4m"
#define ANSI_RESET   "\x1b[0m"

#define _WINSOCK_DEPRECATED_NO_WARNINGS

DWORD WINAPI recvThread() //接收消息线程
{
	while (true)
	{
		char msg[msgSize] = {};//接收消息缓冲区
		if (recv(Client, msg, sizeof(msg), 0) > 0)//参数：客户端套接字，要发送的缓冲区（信息），上一个参数的长度，标志
		{
			cout << endl << msg << endl;
		}
		else if (recv(Client, msg, sizeof(msg), 0) < 0)
		{
			cout << endl << "你已退出或是服务端断开" << endl;
			break;
		}
	}
	Sleep(100);
	return 0;
}

int main()
{
	//初始化 WinSock 库
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 1), &wsaData) == -1) {
		cout << "初始化 WinSock 出错,Error:" << WSAGetLastError;
	}
	else {
		cout << "初始化 WinSock 成功" << endl;
	}
	//创建一个客户端套接字
	Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	/*
	AF_INET：使用ipv4（AF是指定地址族的宏，Address Family），也可以使用PF_INET,在使用Socket API进行套接字编程时二者是等价的
	SOCK_STREAM：套接字类型，保证了数据的有序性，确保了数据的完整性和可靠性，以及提供了流式传输的特性，确保传输的数据按照发送顺序被接收
				 同时还是实验要求的一部分
	IPPROTO_TCP：使用TCP传输协议
	*/

	// 获取本机IP地址
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

	
	char localIp[INET_ADDRSTRLEN];
	char serverIp[INET_ADDRSTRLEN];

	for (struct addrinfo* p = addrInfo; p != nullptr; p = p->ai_next) {
		struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(p->ai_addr);

		if (inet_ntop(AF_INET, &(ipv4->sin_addr), localIp, INET_ADDRSTRLEN) == nullptr) {
			cout << "inet_ntop 失败" << endl;
			exit(EXIT_FAILURE);
		}
		else {
			cout << "本机IP地址 " ANSI_COLOR_MAGENTA << localIp << ANSI_RESET << endl;
		}

		// 判断服务端是否在本地
		cout << "服务端是否在本地？(Y/N): ";
		char choice;
		cin >> choice;
		if (choice == 'Y' || choice == 'y') {
			strcpy_s(serverIp, INET_ADDRSTRLEN, localIp);  // 将ipAddr赋值给serverIp
		}
		else {
			cout << "请输入服务端IP地址(输入N则选择默认IP）: ";
			cin >> serverIp;
			if (!strcmp(serverIp,"n")|| !strcmp(serverIp, "N")) {  // 判断用户是否选择默认ip
				strcpy_s(serverIp, INET_ADDRSTRLEN, ServIp);
			}
		}

	}
	freeaddrinfo(addrInfo);

	// 绑定服务器地址
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(PORT);

	if (inet_pton(AF_INET, serverIp, &(servAddr.sin_addr)) != 1) {
		cout << "服务端地址绑定出错" << endl;
		exit(EXIT_FAILURE);
	}
	else {
		cout << "服务端地址绑定成功";
		cout << ", 地址为 " ANSI_COLOR_MAGENTA << serverIp << ":" << PORT << ANSI_RESET << endl;
	}

	cout << "连接服务端中..." << endl;

	//向服务器发起请求
	if (connect(Client, (SOCKADDR*)&servAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
	{
		cout << "服务端连接失败，Error: " ANSI_COLOR_RED << WSAGetLastError() << ANSI_RESET << endl;
		if (WSAGetLastError() == 10061) {
			cout << "服务端可能未开启\n";
		}
		exit(EXIT_FAILURE);
	}
	else
	{
		cout << "服务端连接成功\n" << endl;
	}


	//创建消息线程
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recvThread, NULL, 0, 0);

	char msg[msgSize] = {};
	cout << "输入 '" ANSI_COLOR_RED << "exit" << ANSI_RESET "' 断开与服务端的连接" << endl;


	//发送消息
	while (true)
	{
		cin.getline(msg, sizeof(msg));
		if (strcmp(msg, "exit") == 0) //输入exit断开
		{
			break;
		}
		else if (strlen(msg) == 0)  // 当输入为空字符串时不发送消息
		{
			continue;
		}
		send(Client, msg, sizeof(msg), 0);//向服务端发送消息
	}

	//关闭客户端套接字以及WinSock库
	closesocket(Client);
	WSACleanup();

	return 0;
}