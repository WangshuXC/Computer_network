#include <iostream>
#include <WINSOCK2.h>
#include <time.h>
#include <fstream>
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

double MAX_TIME = 0.5 * CLOCKS_PER_SEC; // 一秒CPU运行的时钟周期数为1000个，1ms一个时钟周期，所以MAX_TIME为0.5ms

int LOSS = 1; // 丢包率

u_short checkSum(u_short *mes, int size)
{
    int count = (size + 1) / 2;
    // buffer相当于一个元素为u_short类型的数组，每个元素16位，相当于求校验和过程中的一个元素
    u_short *buf = (u_short *)malloc(size + 1);
    memset(buf, 0, size + 1);
    memcpy(buf, mes, size); // 将message读入buf
    u_long sum = 0;
    while (count--)
    {
        sum += *buf++;
        // 如果有进位则将进位加到最低位
        if (sum & 0xffff0000)
        {
            sum &= 0xffff;
            sum++;
        }
    }
    // 取反
    return ~(sum & 0xffff);
}

struct HEADER
{
    u_short sum = 0;      // 校验和 16位
    u_short datasize = 0; // 所包含数据长度 16位
    unsigned char flag = 0;
    // 八位，使用后三位，排列是FIN ACK SYN
    unsigned char SEQ = 0;
    // 八位，传输的序列号，0~255，超过后mod
    HEADER()
    {
        sum = 0;      // 校验和    16位
        datasize = 0; // 所包含数据长度     16位
        flag = 0;     // 8位，使用后四位，排列是FIN ACK SYN
        SEQ = 0;      // 8位
    }
};

int Connect(SOCKET &sockServ, SOCKADDR_IN &ClientAddr, int &ClientAddrLen)
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];

    // 接收第一次握手信息
    while (1)
    {
        // 通过绑定的socket传递、接收数据
        if (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, &ClientAddrLen) == -1)
        {
            return -1;
        }
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == SYN && checkSum((u_short *)&header, sizeof(header)) == 0)
        {
            cout << "[\033[1;32mReceive\033[0m] 接收到第一次握手数据 " << endl;
            break;
        }
    }

    // 发送第二次握手信息
    header.flag = ACK;
    header.sum = 0;
    header.sum = checkSum((u_short *)&header, sizeof(header));
    memcpy(Buffer, &header, sizeof(header));

    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen) == -1)
    {
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第二次握手数据 " << endl;
    }
    clock_t start = clock(); // 记录第二次握手发送时间

    // 接收第三次握手
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, &ClientAddrLen) <= 0)
    {
        // 超时重传
        if (clock() - start > MAX_TIME)
        {
            cout << "[\033[1;33mInfo\033[0m] 第二次握手超时 " << endl;
            header.flag = ACK;
            header.sum = 0;
            header.flag = checkSum((u_short *)&header, sizeof(header));
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen) == -1)
            {
                return -1;
            }
            cout << "[\033[1;33mInfo\033[0m] 已经重传 " << endl;
        }
    }

    // 解析收到的第三次握手的数据包
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));

    if (temp1.flag == ACK_SYN && checkSum((u_short *)&temp1, sizeof(temp1)) == 0)
    {
        cout << "[\033[1;32mReceive\033[0m] 接收到第三次握手数据" << endl;
        cout << "[\033[1;33mInfo\033[0m] 成功连接" << endl;
    }
    else
    {
        cout << "[\033[1;33mInfo\033[0m] 错误数据，请重试" << endl;
    }
    return 1;
}

int RecvMessage(SOCKET &sockServ, SOCKADDR_IN &ClientAddr, int &ClientAddrLen, char *message)
{
    long int fileLength = 0; // 文件长度
    HEADER header;
    char *Buffer = new char[MAXSIZE + sizeof(header)];
    int seq = 0;
    int index = 0;

    while (1)
    {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr *)&ClientAddr, &ClientAddrLen); // 接收报文长度
        if (length <= 0)
            break;
        // 运行到此时已经接收到一个数据包了，否则已经break

        // 丢包测试
        int drop_probability = rand() % 100;
        // cout << drop_probability << endl;
        if (drop_probability < LOSS)
        {
            cout << "[\033[1;34mLoss\033[0m] 模拟丢包" << endl;
            continue;
        }

        memcpy(&header, Buffer, sizeof(header));

        // 判断校验和
        if (checkSum((u_short *)Buffer, length) != 0)
        {
            cout << "[\033[1;33mInfo\033[0m] 数据包出现错误，收到的length为" << length << "已经丢弃，等待重传 " << endl;
            continue; // 丢弃数据包
        }

        // 判断是否是结束
        if (header.flag == OVER)
        {
            cout << "[\033[1;33mInfo\033[0m] 传输结束" << endl;
            break;
        }
        /*
        如果不是希望的序列号，即失序数据包，则重传ack后continue将该收到的数据包丢弃
        如果希望收到3号ack但收到4号，说明3号数据包丢失，重传2号ack。这是如果窗口出现空余，
        发送端会继续发包，但是server每收到一个包都返回一个2号的ack，
        直到发送端收不到包的时候就要进行超时判断，
        超时后重传3号数据包，就可以正常收到3号数据包了。
        */
        if (seq != int(header.SEQ))
        {
            // 说明出了问题，返回ACK
            header.flag = ACK;
            header.datasize = 0;
            header.SEQ = (unsigned char)seq - 1; // 假设已经确认2，希望收到3,但收到4，所以应该返回2的ACK，所以seq要减1
            header.sum = 0;
            u_short temp = checkSum((u_short *)&header, sizeof(header));
            header.sum = temp;
            memcpy(Buffer, &header, sizeof(header));
            // 重发该包的ACK
            sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen);
            cout << "[\033[1;31mSend\033[0m] 非希望的seq，重发给客户端 flag:" << (int)header.flag << " SEQ:" << (int)header.SEQ << " SUM:" << int(header.sum) << endl;
            continue;
        }

        // 收到了希望的序列的数据包
        seq = int(header.SEQ);
        if (seq > 255)
        {
            seq = seq - 256;
        }
        // 取出buffer中的内容
        cout << "[\033[1;32mReceive\033[0m] 收到了 " << length - sizeof(header) << " 字节 - Flag:" << int(header.flag)
             << " SEQ : " << int(header.SEQ) << " SUM:" << int(header.sum) << endl;
        char *temp = new char[length - sizeof(header)];
        memcpy(temp, Buffer + sizeof(header), length - sizeof(header)); // temp中存入当前数据包内容
        // cout << "size" << sizeof(message) << endl;
        memcpy(message + fileLength, temp, length - sizeof(header)); // 把每一个文件数据包中的内容，通过all的偏移，存入message
        fileLength = fileLength + int(header.datasize);

        // 返回ACK
        header.flag = ACK;
        header.datasize = 0;
        header.SEQ = (unsigned char)seq;
        header.sum = 0;
        u_short temp1 = checkSum((u_short *)&header, sizeof(header));
        header.sum = temp1;
        memcpy(Buffer, &header, sizeof(header));
        Sleep(2); // sleep一下再返回ack，延迟返回ack

        sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen);
        cout << "[\033[1;31mSend\033[0m] 回复客户端 - flag:" << (int)header.flag << " SEQ:" << (int)header.SEQ << " SUM:" << int(header.sum) << endl;
        seq++;
        if (seq > 255)
        {
            seq = seq - 256;
        }
    }
    // 发送OVER信息
    header.flag = OVER;
    header.sum = 0;
    u_short temp = checkSum((u_short *)&header, sizeof(header));
    header.sum = temp;
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen) == -1)
    {
        return -1;
    }
    return fileLength;
}

int disConnect(SOCKET &sockServ, SOCKADDR_IN &ClientAddr, int &ClientAddrLen)
{
    HEADER header;
    char *Buffer = new char[sizeof(header)];
    while (1)
    {
        int length = recvfrom(sockServ, Buffer, sizeof(header) + MAXSIZE, 0, (sockaddr *)&ClientAddr, &ClientAddrLen); // 接收报文长度
        memcpy(&header, Buffer, sizeof(header));
        if (header.flag == FIN && checkSum((u_short *)&header, sizeof(header)) == 0)
        {
            cout << "[\033[1;32mReceive\033[0m] 接收到第一次挥手数据 " << endl;
            break;
        }
    }
    // 发送第二次挥手信息
    header.flag = ACK;
    header.sum = 0;
    header.sum = checkSum((u_short *)&header, sizeof(header));
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen) == -1)
    {
        cout << "[\033[1;33mInfo\033[0m] 发送第二次挥手失败";
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第二次挥手数据" << endl;
    }
    clock_t start = clock(); // 记录第二次挥手发送时间

    // 接收第三次挥手
    while (recvfrom(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, &ClientAddrLen) <= 0)
    {
        // 发送第二次挥手等待第三次挥手过程中超时，重传第二次挥手
        if (clock() - start > MAX_TIME)
        {
            cout << "[\033[1;33mInfo\033[0m] 第二次挥手超时 " << endl;
            header.flag = ACK;
            header.sum = 0;
            header.sum = checkSum((u_short *)&header, sizeof(header));
            memcpy(Buffer, &header, sizeof(header));
            if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen) == -1)
            {
                return -1;
            }
            cout << "[\033[1;33mInfo\033[0m] 已重传第二次挥手数据 " << endl;
        }
    }
    // 解析收到的第三次挥手
    HEADER temp1;
    memcpy(&temp1, Buffer, sizeof(header));
    if (temp1.flag == FIN_ACK && checkSum((u_short *)&temp1, sizeof(temp1) == 0))
    {
        cout << "[\033[1;32mReceive\033[0m] 接收到第三次挥手数据 " << endl;
    }
    else
    {
        cout << "[\033[1;33mInfo\033[0m] 错误数据，请重试" << endl;
        return -1;
    }

    // 发送第四次挥手信息
    header.flag = FIN_ACK;
    header.sum = 0;
    header.sum = checkSum((u_short *)&header, sizeof(header));
    memcpy(Buffer, &header, sizeof(header));
    if (sendto(sockServ, Buffer, sizeof(header), 0, (sockaddr *)&ClientAddr, ClientAddrLen) == -1)
    {
        cout << "[\033[1;33mInfo\033[0m] 第四次挥手发送失败 " << endl;
        return -1;
    }
    else
    {
        cout << "[\033[1;31mSend\033[0m] 成功发送第四次挥手数据 " << endl;
    }
    cout << "[\033[1;33mInfo\033[0m] 四次挥手结束，连接断开！ " << endl;
    return 1;
}

int main()
{

    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    SOCKADDR_IN serverAddr;
    SOCKET server;

    serverAddr.sin_family = AF_INET;   // 使用IPV4
    serverAddr.sin_port = htons(8888); // 设置为router的端口
    serverAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

    cout << "输入需要模拟的丢包率(0-100)" << endl;
    cin >> LOSS;

    server = socket(AF_INET, SOCK_DGRAM, 0);
    cout << "[\033[1;33mInfo\033[0m] 成功建立socket " << endl;
    bind(server, (SOCKADDR *)&serverAddr, sizeof(serverAddr)); // 绑定套接字，进入监听状态
    cout << "[\033[1;33mInfo\033[0m] 进入监听状态，等待客户端上线 " << endl;

    int len = sizeof(serverAddr);
    // 建立连接
    Connect(server, serverAddr, len);
    cout << "[\033[1;33mInfo\033[0m] 成功建立连接，正在等待接收文件 " << endl;

    while (true)
    {
        char *name = new char[20];
        char *data = new char[100000000];
        int namelen = RecvMessage(server, serverAddr, len, name);
        int datalen = RecvMessage(server, serverAddr, len, data);
        string a;
        for (int i = 0; i < namelen; i++)
        {
            a = a + name[i];
        }

        cout << endl
             << "[\033[1;36mOut\033[0m] 接收的文件名:" << a << endl;
        ofstream fout(a.c_str(), ofstream::binary);
        cout << "[\033[1;36mOut\033[0m] 接收的文件长度:" << datalen << endl;
        for (int i = 0; i < datalen; i++)
        {
            fout << data[i];
        }
        fout.close();
        cout << "[\033[1;36mOut\033[0m] 文件已成功下载到本地 " << endl
             << endl;

        if (disConnect(server, serverAddr, len) == 1)
        {
            break;
        }
    }

    system("pause");
    return 0;
}
