#pragma warning(disable:4996)
#include<WinSock2.h>
#include<WS2tcpip.h>
#include<iostream>
#include<time.h>
#include <string>
using namespace std;
#pragma  comment(lib,"ws2_32.lib")

string client_ip = "127.0.0.1";//发送端IP
u_short client_port = 8081; //发送端端口
string server_ip = "127.0.0.1"; //接收端IP
u_short server_port = 8082; //接收端端口
SOCKADDR_IN server_addr = { 0 }; //接收端地址
SOCKADDR_IN client_addr = { 0 }; //发送端地址
int seq = 1;//序列号
int ack;//确认序号
const int p_size = 4106; // 1034;
int length = sizeof(SOCKADDR);
SOCKET server_socket = INVALID_SOCKET;//客户端套接字
const int Timeout = 3000;//延时重发

char recvbuffer[40][p_size];//接收缓冲区
int SR_seq = 0;//当前接收的数据包的序列号
int base_seq = 0;//接收基序号，也是当前等待交付上层（即写文件）的数据包的最小序列号
int true_data[40] = { 0 };//记录该部分缓冲区真实可写数据的大小
int true_data_len;

class File
{
private:
	FILE* f;
	char path[_MAX_PATH], drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];
public:
	FILE* File_Select()
	{
		cout << "请选择要传输的文件：";
		cin >> path;
		if (f = fopen(path, "rb"))//以读/写方式打开一个二进制文件，只允许读/写数据。
		{
			cout << "[\033[1;33mInfo\033[0m] 已找到目标文件" << path<< endl;
			return f;
		}
		else
		{
			cout << "[\033[1;33mInfo\033[0m] 未找到该文件不存在，请重新选择。" << endl;;
			return File_Select();
		}
	}
	char* Get_File_Name()//在路径中获取文件名
	{
		_splitpath(path, drive, dir, fname, ext);
		return strcat(fname, ext);
	}
	FILE* Create_File(char* name)
	{
		remove(name);
		f = fopen(name, "ab");//以读/写方式打开一个二进制文件，允许读或在文件末追加数据
		if (!f)	cout << "[\033[1;33mInfo\033[0m] 创建文件失败\n";
		return f;
	}

	bool Create_Dir(char* dir)
	{
		char head[_MAX_PATH] = "md ";
		return system(strcat(head, dir));
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

int main() {
	cout << "\n=================接收端=================\n\n";

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

	/*cout << "请输入服务器端端IP地址：";
	cin >> server_ip;
	cout << "请输入服务器端端口号：";
	cin >> server_port;*/

	SOCKADDR_IN server_addr;

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.S_un.S_addr = inet_addr(server_ip.c_str());

	File file_handle;
	int Timeout = 3000;
	unsigned long ul = 1;
	server_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (server_socket == INVALID_SOCKET) {
		cout << "[\033[1;33mInfo\033[0m] 服务器端创建套接字失败！" << endl;
		WSACleanup();
		return -1;
	}
	cout << "[\033[1;33mInfo\033[0m] 服务器端创建套接字成功!" << endl;

	if (bind(server_socket, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		cout << "[\033[1;33mInfo\033[0m] 服务器端绑定套接字失败!" << endl;
		closesocket(server_socket);
		WSACleanup();
		return -1;
	}
	cout << "[\033[1;33mInfo\033[0m] 绑定成功!" << endl;
	int windowSize;
	cout << "设置窗口大小：";
	cin >> windowSize;
	int seqSize = windowSize * 2;
	while (true)
	{
		//用于创建接收文件的文件
		int err = 0;
		DWORD TIME_OUT = 10;
		char Filename[BUFSIZ] = {};
		char ClientAddr[BUFSIZ] = {};
		char FromName[BUFSIZ] = {};
		FILE* f = NULL;


		//================================开始建立连接============================================
		//初始化接收（客户端发来的）缓冲区和发送缓冲区（待会儿自己要发的）
		char sendBuf[10] = { 0 };
		char receiveBuf[10] = { 0 };

		while (1) {
			if (recvfrom(server_socket, receiveBuf, 10, 0, (SOCKADDR*)&client_addr, &length) > 0) {
				if (receiveBuf[5] == 1) {//即数据报的SYN位为1，标志握手请求报文
					cout << "[\033[1;32mReceive\033[0m] 收到来自" << inet_ntoa(client_addr.sin_addr) << "的建立连接请求，回送建立连接数据报" << endl;

					//设置服务器端要发送的数据报头
					setPacketHead(sendBuf);
					//计算校验和，计算发送过来的数据的校验和
					u_short buf[6] = { 0 };
					int i;
					for (i = 0; i < 10; i += 2) {
						buf[i / 2] = (u_char)receiveBuf[i] << 8;
						if ((i + 1) < 10) {
							buf[i / 2] += (u_char)receiveBuf[i + 1];
						}
					}
					u_short checks = cal_checksum(buf, i / 2);
					if (checks == 0) { //校验和为0，表示数据报传输正确
						//获取数据报（发送端发来的）中的seq
						seq = ((u_char)receiveBuf[0] << 8) + (u_char)receiveBuf[1];
						//回复的数据包中的seq等于发送的数据
						sendBuf[0] = (u_char)(seq >> 8);
						sendBuf[1] = (u_char)(seq & 0xFF);
						//确认序号ack=seq+1
						ack = seq + 1;
						sendBuf[2] = (u_char)(ack >> 8);
						sendBuf[3] = (u_char)(ack & 0xFF);
						//同意建立连接，标志位ACK和SYN设置成1
						sendBuf[4] = 1;//ACK
						sendBuf[5] = 1;//SYN
						//填补上校验和位
						sendBuf[8] = checks >> 8;//校验和
						sendBuf[9] = checks & 0xFF;//校验和
						//发送确认建立连接报文
						sendto(server_socket, sendBuf, 10, 0, (SOCKADDR*)&client_addr, length);
						cout << "服务器端发送确认建立报文！" << endl;
						break;
					}
					else {
						//发来的数据报存在问题，不回复，对方会因为延时重发
						cout << "[\033[1;33mInfo\033[0m] 连接数据报发送出现错误，等待重新连接！" << endl;

					}
				}

			}
			else {
				cout << "[\033[1;33mInfo\033[0m] 连接出错，请等待重新连接" << endl;
			}

		}
		//==============================根据传来的文件名建立目标文件和文件夹===========================
		//回复确认收到文件名缓冲区
		char sendBuf_name[10] = { 0 };
		char* receiveBuf_name = new char[1500];
		memset(receiveBuf_name, 0, 1500);
		while (true) {
			//接收传来的文件名数据报并存在receiveBuf_name中
			if (recvfrom(server_socket, receiveBuf_name, 1500, 0, (SOCKADDR*)&client_addr, &length) > 0) {
				strcpy(ClientAddr, inet_ntoa(client_addr.sin_addr));//char ClientAddr[BUFSIZ] = {}存ip地址
				strcpy(FromName, receiveBuf_name + 10);//跳过数据报头部分
				//没有捕获到有效的文件名，先continue;
				if (FromName == NULL) {
					continue;
				}
				cout << "文件名为：" << FromName << endl;

				string name = FromName;
				int length_name = name.length();
				//建立目标文件
				file_handle.Create_Dir(ClientAddr);
				strcpy(Filename, ClientAddr);
				strcat(Filename, "\\");
				strcat(Filename, receiveBuf_name + 10);
				f = file_handle.Create_File(Filename);
				cout << "[\033[1;33mInfo\033[0m] 开始传输" << Filename << "文件。\n";
				//设置数据报
				seq = ((u_char)receiveBuf_name[0] << 8) + (u_char)receiveBuf_name[1];

				if (receiveBuf_name[5] != 2 && receiveBuf_name[5] != 1 &&
					//数据报的第6个字节表示SYN(1)/FIN(2)
					seq == 1 //表示发送的是文件名
					)
				{
					setPacketHead(sendBuf_name);
					//获取数据的长度
					int len = ((u_char)receiveBuf_name[6] << 8) + (u_char)receiveBuf_name[7];
					u_short* buf = new u_short[(10 + len) / 2 + 1];
					int i;
					for (i = 0; i < 10 + len; i += 2) {
						buf[i / 2] = ((u_char)receiveBuf_name[i] << 8);
						if ((i + 1) < 10 + length_name) {
							buf[i / 2] += (u_char)receiveBuf_name[i + 1];
						}
					}
					u_short checks = cal_checksum(buf, i / 2);
					cout << "[\033[1;32mReceive\033[0m] 接收到的数据报的校验和:" << checks << endl;

					sendBuf_name[0] = receiveBuf_name[0];
					sendBuf_name[1] = receiveBuf_name[1];
					sendBuf_name[2] = receiveBuf_name[2];
					sendBuf_name[3] = receiveBuf_name[3];
					sendBuf_name[4] = 0;//ACK
					sendBuf_name[8] = checks >> 8;//校验和
					sendBuf_name[9] = checks & 0xFF;//校验和

					if (checks == 0) {
						//接收到的数据报没问题
						sendBuf_name[4] = 1;//ACK
						sendto(server_socket, sendBuf_name, 10, 0, (SOCKADDR*)&client_addr, length);
						cout << "[\033[1;33mInfo\033[0m] 文件名传送成功！" << endl;
						break;
					}
					//检测校验和出错，
					else {
						cout << "[\033[1;33mInfo\033[0m] 文件名损坏！重传" << endl;//发送重复的ACK=0，回复自己没有收到
						sendto(server_socket, sendBuf_name, 10, 0, (SOCKADDR*)&client_addr, length);
					}
				}
			}
		}
		//=============================================开始传输文件=====================================================
		clock_t now = clock();
		int sum = 0;
		seq = -1;
		int k = 1;
		int rev = 0;
		base_seq = 0;
		char sendBuf_file[10] = { 0 };
		char* receiveBuf_file = new char[p_size];//接收缓冲区
		memset(receiveBuf_file, 0, p_size);
		//为解决一方意外关闭问题，设为非阻塞式，但只处理rev>0且校验和正确的数据包，通过计时器判断对方是否失联
		ioctlsocket(server_socket, FIONBIO, (unsigned long*)&ul);

		while (1) {
			while (1) {
				//接收发送端传来的数据包
				rev = recvfrom(server_socket, receiveBuf_file, p_size, 0, (SOCKADDR*)&client_addr, &length);
				if (clock() - now <= 8000) {
					//Timeout是3000
					if (clock() - now <= Timeout) {
						if (rev > 0) {
							//结束连接报文，说明文件传输结束
							if (receiveBuf_file[5] == 2) {
								cout << "[\033[1;33mInfo\033[0m] 开始挥手" << endl;
								setPacketHead(sendBuf_file);
								u_short buf[6] = { 0 };
								int i;
								for (i = 0; i < 10; i += 2) {
									buf[i / 2] = (u_char)receiveBuf_file[i] << 8;
									if ((i + 1) < 10) {
										buf[i / 2] += (u_char)receiveBuf_file[i + 1];
									}
								}
								u_short checks = cal_checksum(buf, i / 2);
								if (checks == 0) {
									seq = ((u_char)receiveBuf_file[0] << 8) + (u_char)receiveBuf_file[1];
									sendBuf_file[0] = (u_char)(seq >> 8);
									sendBuf_file[1] = (u_char)(seq & 0xFF);
									ack = seq + 1;
									sendBuf_file[2] = (u_char)(ack >> 8);
									sendBuf_file[3] = (u_char)(ack & 0xFF);
									sendBuf_file[4] = 1;//ACK
									sendBuf_file[5] = 2;//FIN
									sendBuf_file[8] = checks >> 8;//校验和
									sendBuf_file[9] = checks & 0xFF;//校验和
									sendto(server_socket, sendBuf_file, 10, 0, (SOCKADDR*)&client_addr, length);
									cout << "[\033[1;33mInfo\033[0m] 文件" << FromName << "已经成功传输！对方请求断开连接！" << endl;
									cout << "[\033[1;33mInfo\033[0m] 完成挥手过程断开连接" << endl;
									fclose(f);
									closesocket(server_socket);
									WSACleanup();
									system("pause");
									return 0;
								}
								else {
									cout << "[\033[1;33mInfo\033[0m] 断开连接失败！" << endl;
									break;
								}
							}


							//文件数据@@@@

							else {
								SR_seq = ((u_char)receiveBuf_file[0] << 8) + (u_char)receiveBuf_file[1];
								true_data_len = 10 + ((u_char)receiveBuf_file[6] << 8) + (u_char)receiveBuf_file[7];
								//将接收到的数据缓存到recvbuffer
								memcpy(recvbuffer[SR_seq % windowSize], receiveBuf_file, true_data_len);//缓存数据
								true_data[SR_seq % windowSize] = true_data_len;
								cout << "[\033[1;32mReceive\033[0m] 收到序列号为：" << SR_seq << "的数据包";
								setPacketHead(sendBuf_file);
								u_short* buf = new u_short[p_size / 2 + 1];
								memset(buf, 0, p_size / 2 + 1);
								//获取数据的长度，数据报的序列号和下一个序列号
								int length_file = ((u_char)receiveBuf_file[6] << 8) + (u_char)receiveBuf_file[7];
								ack = ((u_char)receiveBuf_file[2] << 8) + (u_char)receiveBuf_file[3];
								//计算校验和
								int i;
								for (i = 0; i < length_file + 10; i += 2) {
									buf[i / 2] = ((u_char)receiveBuf_file[i] << 8);
									if ((i + 1) < length_file + 10) {
										buf[i / 2] += (u_char)receiveBuf_file[i + 1];
									}
								}
								u_short checks = cal_checksum(buf, i / 2);
								sendBuf_file[0] = receiveBuf_file[0];
								sendBuf_file[1] = receiveBuf_file[1];
								sendBuf_file[2] = receiveBuf_file[2];
								sendBuf_file[3] = receiveBuf_file[3];
								sendBuf_file[4] = 0;//ACK
								sendBuf_file[8] = checks >> 8;//校验和
								sendBuf_file[9] = checks & 0xFF;//校验和
								cout << ",数据包的校验和" << checks << endl;
								if (checks == 0) {
									sendBuf_file[4] = 1;//发送ACK确认接收到了文件
									sendto(server_socket, sendBuf_file, 10, 0, (SOCKADDR*)&client_addr, length);
									now = clock();
									cout << "[\033[1;31mSend\033[0m] 发送ACK " << SR_seq << endl;
									if ((SR_seq >= base_seq && SR_seq <= (base_seq + windowSize - 1)) || (SR_seq < base_seq && SR_seq <= (base_seq + windowSize - 1) % seqSize)) {
										int i = 0;
										//从基序号开始读取所有已经按序号到达的数据,从基序号开始
										//得到了需要的序列号，写文件滑动窗口
										while (true_data[(base_seq + i) % windowSize]) {
											fwrite(recvbuffer[(base_seq + i) % windowSize] + 10, 1, true_data[(base_seq + i) % windowSize] - 10, f);//写文件
											ZeroMemory(recvbuffer[(base_seq + i) % windowSize], true_data[(base_seq + i) % windowSize]);
											cout << "[\033[1;33mInfo\033[0m] 写入长度为" << true_data[(base_seq + i) % windowSize] << "序列号为" << (base_seq + i) % seqSize << endl;
											//写入之后就清零
											true_data[(base_seq + i) % windowSize] = 0;
											i++;
										}
										base_seq = (base_seq + i) % seqSize;
										cout << "[\033[1;33mInfo\033[0m] 写入已经按序到达的数据，并向前滑动窗口：" << i << " 。" << "当前基序号为" << base_seq << endl;
									}
									else {
										cout << endl;
										continue;
									}
								}
								else {
									cout << endl;
									continue;
								}
							}
						}
						else {
							if (rev == SOCKET_ERROR) {
								Sleep(20);
								now = clock();
								continue;
							}
						}
					}
				}
				else {
					cout << "[\033[1;33mInfo\033[0m] IP为" << inet_ntoa(client_addr.sin_addr) << "发来的文件" << FromName << "在传输过程中失去连接，删除创建的文件。\n" << endl;
					fclose(f);
					remove(Filename);
					return 0;
				}
			}
		}
	}
	closesocket(server_socket);
	WSACleanup();
	system("pause");
	return 0;
}
