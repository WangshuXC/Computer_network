#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
#include <iomanip>
#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable : 4996)
using namespace std;

const int MAXSIZE = 1024; // 传输缓冲区最大长度

const unsigned char SYN = 0x1;
// 001—— FIN = 0 ACK = 0 SYN = 1

const unsigned char ACK = 0x2;
// 010—— FIN = 0 ACK = 1 SYN = 0

const unsigned char ACK_SYN = 0x3;
// 011—— FIN = 0 ACK = 1 SYN = 1

const unsigned char FIN = 0x4;
// 100—— FIN = 1 ACK = 0 SYN = 0

const unsigned char FIN_ACK = 0x5;
// 101—— FIN = 1 ACK = 0 SYN = 1

const unsigned char OVER = 0x7;
// 结束标志 111—— FIN = 1 ACK = 1 SYN = 1

double MAX_TIME = 0.5 * CLOCKS_PER_SEC;

u_short checkSum(u_short *mes, int size)
{
    int count = (size + 1) / 2;
    u_short *buf = (u_short *)malloc(size + 1);
    memset(buf, 0, size + 1);
    memcpy(buf, mes, size);
    u_long sum = 0;
    while (count--)
    {
        sum += *buf++;
        if (sum & 0xffff0000)
        {
            sum &= 0xffff;
            sum++;
        }
    }
    return ~(sum & 0xffff);
}

struct HEADER
{
    u_short sum = 0;      // 校验和 16位
    u_short datasize = 0; // 所包含数据长度 16位
    // 八位，使用后四位，排列是FIN ACK SYN
    unsigned char flag = 0;
    // 八位，传输的序列号，0~255，超过后mod
    unsigned char SEQ = 0;
    HEADER()
    {
        sum = 0;      // 校验和 16位
        datasize = 0; // 所包含数据长度 16位
        flag = 0;     // 8位，使用后四位，排列是FIN ACK SYN
        SEQ = 0;      // 8位
    }
};

int Connect(SOCKET &socketClient, SOCKADDR_IN &servAddr, int &servAddrlen) // 三次握手建立连接
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];

    // 第一次握手
    header.flag = SYN;
    header.sum = 0; // 校验和置0
    // 计算校验和
    header.sum = checkSum((u_short *)&header, sizeof(header));
    // 将数据头放入buffer
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第一次握手数据" << endl;
    }
    clock_t start = clock(); // 记录发送第一次握手时间

    // 为了函数能够继续运行，设置socket为非阻塞状态
    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);

    // 第二次握手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, &servAddrlen) <= 0)
    {
        // 超时需要重传
        if (clock() - start > MAX_TIME) // 超时，重新传输第一次握手
        {
            cout << "[\033[1;33mInfo\033[0m] 第一次握手超时" << endl;
            header.flag = SYN;
            header.sum = 0;                                            // 校验和置0
            header.sum = checkSum((u_short *)&header, sizeof(header)); // 计算校验和
            memcpy(Buffer, &header, sizeof(header));                   // 将数据头放入Buffer
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
            start = clock();
            cout << "[\033[1;33mInfo\033[0m] 已经重传" << endl;
        }
    }

    // 第二次握手，收到来自接收端的ACK
    // 进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag == ACK && checkSum((u_short *)&header, sizeof(header)) == 0)
    {
        cout << "[\033[1;32mReceive\033[0m] 接收到第二次握手数据" << endl;
    }
    else
    {
        cout << "[\033[1;33mInfo\033[0m] 错误数据，请重试" << endl;
        return -1;
    }

    // 进行第三次握手
    header.flag = ACK_SYN;
    header.sum = 0;
    header.sum = checkSum((u_short *)&header, sizeof(header)); // 计算校验和
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第三次握手数据" << endl;
    }
    cout << "[\033[1;33mInfo\033[0m] 服务器成功连接！可以发送数据" << endl;
    return 1;
}

void send_package(SOCKET &socketClient, SOCKADDR_IN &servAddr, int &servAddrlen, char *message, int len, int &order)
{
    HEADER header;
    char *Buffer = new char[MAXSIZE + sizeof(header)];
    header.datasize = len;
    header.SEQ = static_cast<unsigned char>(order);                 // 序列号
    memcpy(Buffer, &header, sizeof(header));                        // 将计算好校验和的header重新赋值给buffer，此时的buffer可以发送了
    memcpy(Buffer + sizeof(header), message, len);                  // buffer为header+message
    header.sum = checkSum((u_short *)Buffer, sizeof(header) + len); // 计算校验和
    // 发送
    sendto(socketClient, Buffer, len + sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
    cout << "[\033[1;31mSend\033[0m] 发送了" << len << " 字节，"
         << " flag:" << int(header.flag)
         << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
    clock_t start = clock(); // 记录发送时间

    // 接收ack等信息
    while (1)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode); // 将套接口状态改为非阻塞

        while (recvfrom(socketClient, Buffer, MAXSIZE, 0, (sockaddr *)&servAddr, &servAddrlen) <= 0)
        {
            // 超时重传
            if (clock() - start > MAX_TIME)
            {
                cout << "[\033[1;33mInfo\033[0m] 发送数据超时" << endl;
                header.datasize = len;
                header.SEQ = u_char(order); // 序列号
                header.flag = u_char(0x0);
                memcpy(Buffer, &header, sizeof(header));
                memcpy(Buffer + sizeof(header), message, sizeof(header) + len);
                u_short check = checkSum((u_short *)Buffer, sizeof(header) + len); // 计算校验和
                header.sum = check;
                memcpy(Buffer, &header, sizeof(header));
                sendto(socketClient, Buffer, len + sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen); // 发送
                cout << "[\033[1;33mInfo\033[0m] 重新发送数据" << endl;
                start = clock(); // 记录发送时间
            }
            else
                break;
        }
        memcpy(&header, Buffer, sizeof(header)); // 缓冲区接收到信息，放入header，此时header中是收到的数据

        if (checkSum((u_short *)&header, sizeof(header)) == 0 && header.SEQ == u_short(order) && header.flag == ACK)
        {
            cout << "[\033[1;32mReceive\033[0m] 已确认收到 - Flag:" << int(header.flag)
                 << " SEQ:" << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); // 改回阻塞模式
}

void send(SOCKET &socketClient, SOCKADDR_IN &servAddr, int &servAddrlen, char *message, int len)
{
    // 需要传送的数据包个数
    int packagenum = len / MAXSIZE + (len % MAXSIZE != 0);
    int seqnum = 0;
    cout << packagenum << endl;
    for (int i = 0; i < packagenum; i++)
    {
        send_package(socketClient, servAddr, servAddrlen,
                     message + i * MAXSIZE, i == packagenum - 1 ? len - (packagenum - 1) * MAXSIZE : MAXSIZE, seqnum);
        seqnum++;
        if (seqnum > 255)
        {
            seqnum = seqnum - 256;
        }
    }
    // 发送结束信息
    HEADER header;
    char *Buffer = new char[sizeof(header)];
    header.flag = OVER;
    header.sum = checkSum((u_short *)&header, sizeof(header));
    memcpy(Buffer, &header, sizeof(header));
    sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
    cout << "[\033[1;31mSend\033[0m] 发送OVER信号" << endl;
    clock_t start = clock();
    while (1)
    {
        u_long mode = 1;
        ioctlsocket(socketClient, FIONBIO, &mode);
        // 收到包，存在Buffer中
        while (recvfrom(socketClient, Buffer, MAXSIZE, 0, (sockaddr *)&servAddr, &servAddrlen) <= 0)
        {
            // 超时，把“send end”数据包重传
            if (clock() - start > MAX_TIME)
            {
                cout << "[\033[1;33mInfo\033[0m] 发送OVER信号超时" << endl;
                char *Buffer = new char[sizeof(header)];
                header.flag = OVER;
                header.sum = checkSum((u_short *)&header, sizeof(header));
                memcpy(Buffer, &header, sizeof(header));
                sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
                cout << "[\033[1;33mInfo\033[0m] 已经重发OVER信号" << endl;
                start = clock();
            }
        }
        memcpy(&header, Buffer, sizeof(header)); // 缓冲区接收到信息，读取Buffer里的信息
        u_short check = checkSum((u_short *)&header, sizeof(header));
        // 收到的数据包为OVER则已经成功接受文件
        if (header.flag == OVER && check == 0)
        {
            cout << "[\033[1;33mInfo\033[0m] 对方已成功接收文件" << endl;
            break;
        }
        else
        {
            continue;
        }
    }
    u_long mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode); // 改回阻塞模式
}

int disConnect(SOCKET &socketClient, SOCKADDR_IN &servAddr, int &servAddrlen)
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];

    u_short sum;

    // 进行第一次挥手
    header.flag = FIN;
    header.sum = 0;                                            // 校验和置0
    header.sum = checkSum((u_short *)&header, sizeof(header)); // 计算校验和
    memcpy(Buffer, &header, sizeof(header));                   // 将首部放入缓冲区
    if (sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第一次挥手数据" << endl;
    }
    clock_t start = clock(); // 记录发送第一次挥手时间

    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode); // FIONBIO为命令，允许1/禁止0套接口s的非阻塞1/阻塞0模式。

    // 接收第二次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, &servAddrlen) <= 0)
    {
        // 超时，重新传输第一次挥手
        if (clock() - start > MAX_TIME)
        {
            cout << "[\033[1;33mInfo\033[0m] 第一次挥手超时" << endl;
            header.flag = FIN;
            header.sum = 0;                                            // 校验和置0
            header.sum = checkSum((u_short *)&header, sizeof(header)); // 计算校验和
            memcpy(Buffer, &header, sizeof(header));                   // 将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
            start = clock();
            cout << "[\033[1;33mInfo\033[0m] 已重传第一次挥手数据" << endl;
        }
    }

    // 进行校验和检验
    memcpy(&header, Buffer, sizeof(header));
    if (header.flag == ACK && checkSum((u_short *)&header, sizeof(header) == 0))
    {
        cout << "[\033[1;32mReceive\033[0m] 接收到第二次挥手数据" << endl;
    }
    else
    {
        cout << "[\033[1;33mInfo\033[0m] 错误数据，请重试" << endl;
        return -1;
    }

    // 进行第三次挥手
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = checkSum((u_short *)&header, sizeof(header)); // 计算校验和
    if (sendto(socketClient, (char *)&header, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen) == -1)
    {
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第三次挥手数据" << endl;
    }
    start = clock();
    // 接收第四次挥手
    while (recvfrom(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME) // 超时，重新传输第三次挥手
        {
            cout << "[\033[1;33mInfo\033[0m] 第三次挥手超时" << endl;
            header.flag = FIN;
            header.sum = 0;                                            // 校验和置0
            header.sum = checkSum((u_short *)&header, sizeof(header)); // 计算校验和
            memcpy(Buffer, &header, sizeof(header));                   // 将首部放入缓冲区
            sendto(socketClient, Buffer, sizeof(header), 0, (sockaddr *)&servAddr, servAddrlen);
            start = clock();
            cout << "[\033[1;33mInfo\033[0m] 已重传第三次挥手数据" << endl;
        }
    }
    cout << "[\033[1;33mInfo\033[0m] 四次挥手结束，连接断开！" << endl;
    return 1;
}

int main()
{
    cout << MAX_TIME << endl;

    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    SOCKADDR_IN serverAddr;
    SOCKET server;

    serverAddr.sin_family = AF_INET; // 使用IPV4
    serverAddr.sin_port = htons(8888);
    serverAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

    server = socket(AF_INET, SOCK_DGRAM, 0);
    int len = sizeof(serverAddr);

    // 建立连接
    if (Connect(server, serverAddr, len) == -1)
    {
        return 0;
    }

    bool flag = true;
    while (true)
    {
        cout << endl
             << "选择你要进行的操作" << endl
             << "1. 退出" << endl;
        if (flag)
        {
            cout << "2. 传输文件" << endl;
            flag = !flag;
        }
        else
        {
            cout << "2. 继续传输文件" << endl;
        }

        int choice = {};

        cin >> choice;
        cout << endl;
        if (choice == 1)
            break;
        else
        {

            // 读取文件内容到buffer
            string inputFile; // 希望传输的文件名称
            cout << "请输入希望传输的文件名称" << endl;
            cin >> inputFile;
            ifstream fileIN(inputFile.c_str(), ifstream::binary); // 以二进制方式打开文件
            char *buffer = new char[100000000];                   // 文件内容
            int i = 0;
            unsigned char temp = fileIN.get();
            while (fileIN)
            {
                buffer[i++] = temp;
                temp = fileIN.get();
            }
            fileIN.close();

            // 发送文件名
            send(server, serverAddr, len, (char *)(inputFile.c_str()), inputFile.length());
            clock_t start1 = clock();
            // 发送文件内容（在buffer里）
            send(server, serverAddr, len, buffer, i);
            clock_t end1 = clock();

            cout << "[\033[1;36mOut\033[0m] 传输总时间为:" << (end1 - start1) / CLOCKS_PER_SEC << "s" << endl;
            cout << "[\033[1;36mOut\033[0m] 吞吐率为:" << fixed << setprecision(2) << (((double)i) / ((end1 - start1) / CLOCKS_PER_SEC)) << "byte/s" << endl;
        }
    }

    disConnect(server, serverAddr, len);
    system("pause");
    return 0;
}
