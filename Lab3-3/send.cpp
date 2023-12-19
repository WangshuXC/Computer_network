#pragma warning(disable:4996)
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iostream>
#include <math.h>
#include <fstream>
#include <cstdio>
#include <string.h>
#include <time.h>
#pragma  comment(lib,"ws2_32.lib")
using namespace std;
int length = sizeof(SOCKADDR);
const int p_size = 4106;

string client_ip = "127.0.0.1";//发送端IP
u_short client_port = 8080; //发送端端口
string server_ip = "127.0.0.1"; //接收端IP
u_short server_port = 8081; //接收端端口
int seq = 1;//序列号
int ack;//确认序号
int totallength = 0;

//=======================================================增加一些滑动窗口需要的环境变量===========================================
int windowSize = 0;
char buffer_resent[50][p_size];//选择重传缓冲区
int SR_seq = 0;//当前发送的数据包的序列号
int base_seq = 0;//发送基序号，当前等待被确认的数据包的最小序列号
bool isacked[100] = { 0 };//记录序号为i的数据包是否已经收到了ACK，和lastend、是否在当前窗口内联合一起判断是否需要重传
clock_t issend[100];//记录序号为i的数据包上一次发送的时间

clock_t last_ack;//记录最近一次接收到对方消息的时间，避免对方离线状态下仍在长时间等待
char receive_buf_file[10] = { 0 };//接收服务器端消息的缓冲区
void recv(void* a);

SOCKET client_socket = INVALID_SOCKET;//发送端套接字
//传输的文件
class File
{
private:
	std::ifstream a;
	FILE* f;
	char path[_MAX_PATH];//完整的路径和文件名
	char drive[_MAX_DRIVE];//驱动器名加冒号
	char dir[_MAX_DIR];//路径
	char fname[_MAX_FNAME];//文件名
	char ext[_MAX_EXT];//后缀 (扩展名)
public:
	FILE* Select_file()
	{
		cout << "选择要传输的文件：";
		cin >> path;
		if (f = fopen(path, "rb"))//以读/写方式打开一个二进制文件，只允许读/写数据。
		{
			cout << "[\033[1;33mInfo\033[0m] 已找到目标文件，可以进行传输。" << endl;
			return f;
		}
		else
		{
			cout << "[\033[1;33mInfo\033[0m] 未找到该文件不存在，请重新选择。" << endl;;
			return Select_file();
		}
	}
	char* Get_File_Name()//在路径中获取文件名
	{
		_splitpath(path, drive, dir, fname, ext);
		return strcat(fname, ext);//返回文件名字
	}
};

//设置数据报头部内容
void setPacketHead(char* Buf) {
	Buf[0] = 0;//seq
	Buf[1] = 0;//seq
	Buf[2] = 0;//ack
	Buf[3] = 0;//ack
	Buf[4] = 0;//ACK
	Buf[5] = 0;//SYN/FIN
	Buf[6] = 0;//长度
	Buf[7] = 0;//长度
	Buf[8] = 0;//校验和
	Buf[9] = 0;//校验和
}

//计算校验和并生成最终发送的数据包
//差错检测，计算校验和
u_short cal_checksum(u_short* buf, int count) {
	unsigned int sum = 0;
	for (int i = 0; i < count; i++) {
		sum += *buf++;
		sum = (sum >> 16) + (sum & 0xFFFF);

	}
	return (u_short)~(sum & 0xFFFF);
}

int main()
{
	cout << "\n=================发送端=================\n\n";
	//初始化套接字
	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData))
	{
		cout << "[\033[1;33mInfo\033[0m] 初始化套接字失败！" << endl;
		return 0;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		cout << "[\033[1;33mInfo\033[0m] 套接字版本不符！" << endl;
		WSACleanup();
		return 0;
	}


	//cout << "请输入客户端IP地址：";
	//cin >> client_ip;
	//cout << "请输入客户端端口号：";
	//cin >> client_port;

	SOCKADDR_IN client_addr;

	client_addr.sin_family = AF_INET;
	client_addr.sin_port = htons(client_port);
	client_addr.sin_addr.S_un.S_addr = inet_addr(client_ip.c_str());

	File file_handle;

	int Timeout = 3000;//3s
	//创建套接字
	client_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (client_socket == INVALID_SOCKET) {
		cout << "[\033[1;33mInfo\033[0m] 客户端创建套接字失败！" << endl;
		WSACleanup();
		return -1;
	}
	cout << "[\033[1;33mInfo\033[0m] 客户端创建套接字成功！" << endl;

	//绑定套接字
	if (bind(client_socket, (SOCKADDR*)&client_addr, sizeof(client_addr)) == SOCKET_ERROR) {
		cout << "[\033[1;33mInfo\033[0m] 客户端绑定套接字失败!" << endl;
		closesocket(client_socket);
		WSACleanup();
		return -1;
	}

	cout << "[\033[1;33mInfo\033[0m] 绑定套接字成功!" << endl;

	//cout << "请输入服务端IP地址：";
	//cin >> server_ip;
	//cout << "请输入服务端端口号：";
	//cin >> server_port;

	SOCKADDR_IN server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.S_un.S_addr = inet_addr(server_ip.c_str());

	//设置套接字为非阻塞模式
	unsigned long ul;
	ioctlsocket(client_socket, FIONBIO, (unsigned long*)&ul);//设置为非阻塞式，用于实现超时重传，否则接收不到消息会被阻塞在recvfrom()

	cout << "设置窗口大小：";
	cin >> windowSize;
	int seqSize = windowSize * 2;
	while (true)
	{
		//输入想要传输的文件
		FILE* f = file_handle.Select_file();

		//========================================开始建立连接=========================================================
		char send_buf[10] = { 0 };
		char recv_buf[10] = { 0 };
		//设置发送端（客户端）数据包头部
		setPacketHead(send_buf);
		//sendData是数据包中发送的数据部分

		seq = rand() % 100;//得到随机数j，并将j保存到seq对应位置
		send_buf[0] = (u_char)(seq >> 8);//取seq的高八位
		send_buf[1] = (u_char)(seq & 0xFF);//取seq的低八位

		//客户端发起握手，设置标志位SYN
		send_buf[5] = 1; //SYN，表示握手报文

		//将char型数据包两位两位拼接并进行校验和
		u_short buf[6] = { 0 };
		int i;
		for (i = 0; i < 10; i += 2) {
			buf[i / 2] = ((u_char)send_buf[i] << 8);//send_buf[i]存在buf的高8位
			if ((i + 1) < 10) {
				buf[i / 2] += (u_char)send_buf[i + 1];//send_buf[i + 1]存在buf的低八位
			}
		}

		//计算校验和
		u_short check = cal_checksum(buf, i / 2);

		//将校验和填到对应位置
		send_buf[8] = (u_char)(check >> 8);//校验和
		send_buf[9] = (u_char)(check & 0xFF);//校验和

		//发送握手请求报文
		sendto(client_socket, send_buf, 10, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));

		int  st = clock();//记录当前时间，超时重传
		int recive = 0;

		while (1) {//等待ACK
			recive = recvfrom(client_socket, recv_buf, 10, 0, (SOCKADDR*)&server_addr, &length);
			int st1 = clock() - st;

			if (recive > 0 && st1 <= Timeout) {
				//获取服务器端确认报文的ack
				ack = ((u_char)recv_buf[2] << 8) + (u_char)recv_buf[3];
				//ack=seq+1,代表握手成功,seq是客户端的seq
				if (seq + 1 == ack)
				{
					cout << "[\033[1;33mInfo\033[0m] 成功与目标服务器建立了连接！" << endl;
					break;
				}
				else {
					//收到重复ACK或者是超时，则重传
					cout << "[\033[1;33mInfo\033[0m] 重新建立连接" << endl;
					sendto(client_socket, send_buf, 10, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));
					st = clock();//更新时间
				}
			}
			else {
				if (recive == SOCKET_ERROR) {
					continue;
				}
				//超时，则重传
				if (st1 >= Timeout) {
					cout << "[\033[1;33mInfo\033[0m] 超时，重新建立连接" << endl;
					sendto(client_socket, send_buf, 10, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));
					st = clock();//更新时间
				}
			}
		}
		//==============================================传送文件名====================================
		//第一个数据包要传送文件名

		char* Filename = file_handle.Get_File_Name();
		cout << "[\033[1;33mInfo\033[0m] 开始传输" << Filename << endl;

		string filename;
		filename = Filename;
		//传输的数据内容为文件名
		string data_segment = filename.c_str();
		//读取文件名数据到数据缓冲区中
		//字符串是英文，字节长度和字符串长度一致
		int length_name = filename.length();

		char* buffer_name = new char[length_name + 1];
		//接收到的服务器端发送来的数据报
		char recv_buf_name[10] = { 0 };
		ZeroMemory(buffer_name, length_name + 1);
		//buffer_name中存放的是传送的数据名
		for (int i = 0; i <= length_name; i++) {
			buffer_name[i] = data_segment[i];
		}
		buffer_name[data_segment.length()] = '\0';

		int alllength = 10 + length_name + 1;
		char* send_buf_name = new char[alllength];
		seq = 1;//发送文件名时，设置为1
		int resend = 0;

		//设置数据报,对于文件名字不再切包
		//设置发送的数据报
		if (!resend) {
			setPacketHead(send_buf_name);

			//将缓冲区对应段数据放入数据报对应位置
			for (int i = 0; i <= length_name; i++) {
				send_buf_name[i + 10] = (u_char)buffer_name[i];
			}
			cout << "[\033[1;33mInfo\033[0m] 客户端传送的文件名为" << send_buf_name + 10 << endl;
			//保存长度
			send_buf_name[6] = length_name + 1 >> 8;//长度
			send_buf_name[7] = length_name + 1 & 0xFF;//长度
			//保存发送名字数据包序列号。设置序列号为1
			send_buf_name[0] = (u_char)(seq >> 8);
			send_buf_name[1] = (u_char)(seq & 0xFF);
			//直接令发送名字数据包的时候ack=0;
			ack = 0;
			send_buf_name[2] = (u_char)(ack >> 8);
			send_buf_name[3] = (u_char)(ack & 0xFF);
			//将数据包两位两位拼接进行校验
			u_short* buf_name = new u_short[alllength / 2 + 1];
			memset(buf_name, 0, alllength / 2 + 1);
			int i;
			for (i = 0; i <= 10 + length_name; i += 2) {
				buf_name[i / 2] = ((u_char)send_buf_name[i] << 8);
				if ((i + 1) <= 10 + length_name) {
					buf_name[i / 2] += (u_char)send_buf_name[i + 1];
				}
			}
			u_short checks = cal_checksum(buf_name, i / 2);
			//将校验和字段保存
			send_buf_name[8] = checks >> 8;//校验和
			send_buf_name[9] = checks & 0xFF;//校验和

			//发送数据包
			cout << "[\033[1;33mInfo\033[0m] 名字校验和为" << checks << endl;
			sendto(client_socket, send_buf_name, alllength, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));//发送文件名
			cout << "[\033[1;31mSend\033[0m] 成功发送了名字数据报" << endl;
			st = clock();

			while (1) {//等待ACK
				recive = recvfrom(client_socket, recv_buf_name, 10, 0, (SOCKADDR*)&server_addr, &length);
				if (clock() - st <= Timeout && recive > 0) {
					//接收到的序列号和发送的序列号相同
					if (recv_buf_name[0] == send_buf_name[0] && recv_buf_name[1] == recv_buf_name[1]) {
						//接收到了ACK
						if (recv_buf_name[4] == 1) {
							cout << "[\033[1;33mInfo\033[0m] 文件名传输成功！\n";
							break;
						}
						else {
							cout << "[\033[1;31mSend\033[0m] 接收到的服务器端没有ACK,重新传输文件名\n";
							sendto(client_socket, send_buf_name, alllength, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));//发送文件名
							st = clock();
						}
					}
					else {
						cout << "[\033[1;31mSend\033[0m] 接收到的文件序号错误，重新传输文件名\n";
						sendto(client_socket, send_buf_name, alllength, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));//发送文件名
						st = clock();
					}
				}
				else {
					if (recive < 0) {
						st = clock();
						continue;
					}
					cout << "[\033[1;31mSend\033[0m] 超时，重新传输文件名\n";
					sendto(client_socket, send_buf_name, alllength, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));//发送文件名
					st = clock();
				}
			}


			//传输文件阶段@@@@

			char* buffer = new char[4096];
			memset(buffer, 0, sizeof(char) * 4096);
			char* send_buf_file = new char[p_size];
			memset(send_buf_file, 0, p_size);
			int all = 0;//记录总字节数
			int count = 0;
			bool success = 1;
			base_seq = 0;
			SR_seq = -1;//第一个发送的包从0开始，和baseseq一致
			ioctlsocket(client_socket, FIONBIO, (unsigned long*)&ul);
			clock_t whole_time = clock();//开始计时
			last_ack = clock();//记录收到上一个ACK的时间
			cout << "[\033[1;33mInfo\033[0m] 开始发送文件" << filename << "..." << endl;
			while (1) {
				//设置一个时间阈值，如果超过就退出连接
				if (clock() - last_ack > 100000) {
					cout << "[\033[1;33mInfo\033[0m] 自动断开连接" << endl;
					success = 0;
					break;
				}
				//分包传输文件
				//传输的序号在窗口范围之内,seq+1从0开始
				if (((((SR_seq + 1) % seqSize) >= base_seq) && (SR_seq + 1) % seqSize < base_seq + windowSize) || (((SR_seq + 1) % seqSize) < base_seq &&
					(SR_seq + 1) % seqSize < (base_seq + windowSize) % seqSize)) {
					if ((count = fread(buffer, 1, p_size - 10, f)) > 0) {
						all += count + 10;
						SR_seq = (SR_seq + 1) % seqSize;
						ZeroMemory(buffer_resent[SR_seq % windowSize], p_size);
						setPacketHead(buffer_resent[SR_seq % windowSize]);//设置报头部分
						//设置数据报
						for (int i = 0; i < count; i++) {
							buffer_resent[SR_seq % windowSize][i + 10] = (u_char)buffer[i];
						}
						//保存长度
						buffer_resent[SR_seq % windowSize][6] = count >> 8;
						buffer_resent[SR_seq % windowSize][7] = count & 0xFF;
						//保存序列号
						buffer_resent[SR_seq % windowSize][0] = (u_char)(SR_seq >> 8);
						buffer_resent[SR_seq % windowSize][1] = (u_char)(SR_seq & 0xFF);
						//如果之后还有包，将下一个包的序列号保存在ack
						//ack从1开始
						ack = SR_seq + 1;
						//最后一个包ack设置为0
						if (count < 4096) {
							cout << "[\033[1;33mInfo\033[0m] 数据包发送完毕" << endl;
							ack = 0;
						}
						buffer_resent[SR_seq % windowSize][2] = (u_char)(ack >> 8);
						buffer_resent[SR_seq % windowSize][3] = (u_char)(ack & 0xFF);
						//将数据包两位两位拼接进行校验
						u_short* buf = new u_short[p_size / 2 + 1];
						memset(buf, 0, p_size / 2 + 1);
						int i;
						for (i = 0; i < 10 + count; i += 2) {
							buf[i / 2] = ((u_char)buffer_resent[SR_seq % windowSize][i] << 8);
							if ((i + 1) < 10 + count) {
								buf[i / 2] += (u_char)buffer_resent[SR_seq % windowSize][i + 1];
							}
						}
						u_short checks = cal_checksum(buf, i / 2);
						//将校验和字段保存
						buffer_resent[SR_seq % windowSize][8] = checks >> 8;
						buffer_resent[SR_seq % windowSize][9] = checks & 0xFF;
						if (SR_seq < base_seq) {
							cout << "[\033[1;33mInfo\033[0m] 当前可用窗口大小：" << (base_seq + windowSize) % seqSize - SR_seq << endl;
						}
						else cout << "[\033[1;33mInfo\033[0m] 当前可用窗口大小：" << (base_seq + windowSize) - SR_seq << endl;
						//发送数据报
						sendto(client_socket, buffer_resent[SR_seq % windowSize], p_size, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));
						Sleep(20);
						issend[SR_seq] = clock();
						//清空buf
						memset(buffer, 0, sizeof(char) * 4096);
						cout << "[\033[1;31mSend\033[0m] 发送序列号为" << SR_seq << "的数据包。";
						cout << "baseseq(发送基序号）:" << base_seq << " seq(当前发送序号):" << SR_seq << " \n";
					}
				}
				//判断当前窗口内是否有需要重传的数据包
				for (int i = 0; i < windowSize; i++) {
					//已经发送过某序号的数据包
					if (issend[(base_seq + i) % seqSize]) {//该号数据包没有收到接收端发来的ACK并且超时需要重传
						if (isacked[(base_seq + i) % seqSize] == 0 && clock() - issend[(base_seq + i) % seqSize] > Timeout)
						{//进行重传
							int sseq = 0;
							sseq = ((u_char)buffer_resent[(base_seq + i) % seqSize % windowSize][0] << 8) + (u_char)buffer_resent[(base_seq + i) % seqSize % windowSize][1];
							int cchecks = 0;
							cchecks = ((u_char)buffer_resent[(base_seq + i) % seqSize % windowSize][8] << 8) + (u_char)buffer_resent[(base_seq + i) % seqSize % windowSize][9];
							cout << "[\033[1;31mSend\033[0m] 超时，重传序列号为" << sseq << "的数据包。";
							cout << "baseseq:" << base_seq << " seq:" << SR_seq << " ";
							cout << endl;
							sendto(client_socket, buffer_resent[(base_seq + i) % seqSize % windowSize], p_size, 0, (SOCKADDR*)&server_addr, sizeof(SOCKADDR));
							issend[(base_seq + i) % seqSize] = clock();
						}
					}
				}
				int rev = recvfrom(client_socket, receive_buf_file, 10, 0, (SOCKADDR*)&server_addr, &length);
				if (rev > 0) {
					if (receive_buf_file[4] == 1) {//接收到了ACK
						last_ack = clock();
						int recvseq = ((u_char)receive_buf_file[0] << 8) + (u_char)receive_buf_file[1];
						int  aack = ((u_char)receive_buf_file[2] << 8) + (u_char)receive_buf_file[3];
						if ((recvseq >= base_seq && recvseq <= (base_seq + windowSize - 1)) || (recvseq < base_seq && recvseq <= (base_seq + windowSize - 1) % seqSize))
						{
							//收到在当前窗口范围内的ACK
							isacked[recvseq] = 1;
							issend[recvseq] = 0;
							//如果收到的恰好为发送基序号的ACK，就滑动窗口
							if (recvseq == base_seq) {
								int i = 0;//记录可以滑动窗口的大小
								while (isacked[(base_seq + i) % seqSize]) {
									isacked[(base_seq + i) % seqSize] = 0;//滑动之后就将这一位置0
									i++;
								}
								//得到当前新的发送基序号
								base_seq = (base_seq + i) % seqSize;
								cout << "[\033[1;32mReceive\033[0m] 收到ACK " << recvseq << "，向前滑动窗口：" << i << " 。" << endl;
							}
							if (aack == 0) {
								//所有文件都已经传送完成了并且接收到了ACK；
								cout << "[\033[1;33mInfo\033[0m] 文件传送完成，跳出循环" << endl;
								break;
							}
							continue;
						}
					}
					else {
						//无效数据包不做处理
						continue;
					}
				}
				else {
					//无效数据包不做处理
					continue;
				}
			}
			//==================================================文件传送完成，关闭本次链接==============================
			if (success) {
				cout << "[\033[1;33mInfo\033[0m] 文件" << Filename << "已传送完成，向对方发出关闭连接的请求。\n";

				int all_time = clock() - whole_time;
				cout << "[\033[1;36mOut\033[0m] 传输用时: " << all_time << "ms" << endl;
				cout << "[\033[1;36mOut\033[0m] 平均吞吐率: " << double(all) / double(all_time / 1000) << " byte/s \n\n";

				//两次挥手断开连接
				char sendBuf_bye[10] = { 0 };
				char receiveBuf_bye[10] = { 0 };
				seq = rand() % 100;
				sendBuf_bye[0] = (u_char)(seq >> 8);
				sendBuf_bye[1] = (u_char)(seq & 0xFF);
				sendBuf_bye[5] = 2;//FIN
				u_short buf[6] = { 0 };
				int i;
				for (i = 0; i < 10; i += 2) {
					buf[i / 2] = ((u_char)sendBuf_bye[i] << 8);
					if ((i + 1) < 10) {
						buf[i / 2] += (u_char)sendBuf_bye[i + 1];
					}
				}
				u_short checks = cal_checksum(buf, i / 2);
				sendBuf_bye[8] = (u_char)(checks >> 8);//校验和
				sendBuf_bye[9] = (u_char)(checks & 0xFF);//校验和
				sendto(client_socket, sendBuf_bye, 10, 0, (SOCKADDR*)&server_addr, length);
				int res = 0;
				while (true) {
					if (recvfrom(client_socket, receiveBuf_bye, 10, 0, (SOCKADDR*)&server_addr, &length) > 0) {
						if (receiveBuf_bye[5] == 2 && receiveBuf_bye[4] == 1) {
							ack = ((u_char)receiveBuf_bye[2] << 8) + (u_char)receiveBuf_bye[3];
							if (seq + 1 == ack) {
								cout << "[\033[1;33mInfo\033[0m] 成功断开连接" << endl;
								closesocket(client_socket);
								WSACleanup();
								system("pause");
								return 0;
							}
							else {
								cout << "[\033[1;33mInfo\033[0m] 断开连接失败" << endl;
							}
							break;
						}
					}
					else {//重传
						sendto(client_socket, sendBuf_bye, 10, 0, (SOCKADDR*)&server_addr, length);
					}
				}
			}
			else {
				cout << "\n[\033[1;33mInfo\033[0m] 对方中途退出，文件传输失败";
				closesocket(client_socket);
				WSACleanup();
				system("pause");
				return 0;
			}
		}
	}
	closesocket(client_socket);
	WSACleanup();
	system("pause");
	return 0;
}
