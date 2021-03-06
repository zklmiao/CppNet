#ifndef _EasyTcpServer1_7_hpp_
#define _EasyTcpServer1_7_hpp_

#ifdef _WIN32
#define FD_SETSIZE      2506
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include<windows.h>
#include<WinSock2.h>
#pragma comment(lib,"ws2_32.lib")
#else
#include<unistd.h> //uni std
#include<arpa/inet.h>
#include<string.h>

#define SOCKET int
#define INVALID_SOCKET  (SOCKET)(~0)
#define SOCKET_ERROR            (-1)
#endif

#include<stdio.h>
#include<vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <iostream>
#include <algorithm>

#include"MessageHeader.hpp"
#include "CELLTimestamp.hpp"

//缓冲区最小单元大小
#ifndef RECV_BUFF_SZIE
#define RECV_BUFF_SZIE 10240
#endif // !RECV_BUFF_SZIE
#define _Cell_THREAD_CONT 4

class ClientSocket
{
private:
	SOCKET _sockfd;
	char _szMsgBuf[RECV_BUFF_SZIE * 10];
	int _lastPos;
public:
	ClientSocket(SOCKET sock = INVALID_SOCKET)
	{
		_sockfd = sock;
		memset(_szMsgBuf, 0, sizeof(_szMsgBuf));
		_lastPos = 0;
	}
	SOCKET sockfd()
	{
		return _sockfd;
	}
	char* msgBuf()
	{
		return _szMsgBuf;
	}
	int getLastPos()
	{
		return _lastPos;
	}
	void setLastPos(int pos)
	{
		_lastPos = pos;
	}
};

// Delegation [consignor]
// {CellServer -infomation-> INetEvent -infomation-> EasyTcpServer(main work thread)}
class INetEvent
{
public:
	//client leave event.
	virtual void OnLeave(ClientSocket* pClient) = 0;
	// request event
	virtual void OnNetMsg(ClientSocket* pClient, DataHeader* header) = 0;
	// client join event
	virtual void OnNetJoin(ClientSocket* pClient) = 0;
	// receive data event
	virtual void OnNetRecv(ClientSocket* pClient) = 0;
};

class CellServer
{
private:
	SOCKET _sock;
	//Client sequen
	std::map<SOCKET, ClientSocket*>_clients;
	//Client sequen buff
	std::vector<ClientSocket*>_clientsBuff;
	std::mutex _mutex;
	std::thread* _pThread;
	INetEvent* _pNetEvent;
public:
	std::atomic_int _recvCount;

	CellServer(SOCKET sock = INVALID_SOCKET)
	{
		_sock = sock;
		_pNetEvent = nullptr;
	}
	~CellServer()
	{
		Close();
		_sock = INVALID_SOCKET;
		delete _pThread;
	}

	void setEventObj(INetEvent* event)
	{
		_pNetEvent = event;
	}

	void Close()
	{
		if (_sock != INVALID_SOCKET)
		{
#ifdef _WIN32
			for (auto iter : _clients)
			{
				closesocket(iter.second->sockfd());
				delete iter.second;
			}
			closesocket(_sock);
#else
			for (auto iter : _clients)
			{
				closesocket(iter->second->sockfd());
				delete iter->second;
			}
			close(_sock);
#endif
			_clients.clear();
		}
	}

	bool isRun()
	{
		return _sock != INVALID_SOCKET;
	}

	fd_set _fdRead_bak;
	// 有客户端加入 或者退出 集合 fd_set(fdRead) 改变
	bool _clients_change;
	SOCKET _maxSock;
	bool OnRun()
	{
		_clients_change = true;
		while (isRun())
		{
			if (_clientsBuff.size() > 0)
			{
				std::lock_guard<std::mutex>lock(_mutex);
				for (auto pClient : _clientsBuff)
				{
					_clients[pClient->sockfd()] = pClient;
				}
				_clientsBuff.clear();
				_clients_change = true;
			}
			if (_clients.empty())
			{
				std::chrono::milliseconds t(1);
				std::this_thread::sleep_for(t);
				continue;
			}

			fd_set fdRead;

			FD_ZERO(&fdRead);

			if (_clients_change)
			{
				_clients_change = false;
				_maxSock = _clients.begin()->second->sockfd();
				for (auto iter : _clients)
				{
					FD_SET(iter.second->sockfd(), &fdRead);
					_maxSock < iter.second->sockfd() ? _maxSock = iter.second->sockfd() : _maxSock;
				}
				memcpy(&_fdRead_bak, &fdRead, sizeof(fd_set));
			}
			else
			{
				memcpy(&fdRead, &_fdRead_bak, sizeof(fd_set));
			}

			timeval tv{ 0,0 };
			// 若要在CellServer中处理其他业务则用非阻塞模式
			int ret = (int)select(_maxSock + 1, &fdRead, nullptr, nullptr, &tv);
			if (ret < 0)
			{
				printf("Select done.\n");
				Close();
				return false;
			}
			else if (ret == 0)
			{
				continue;
			}

#ifdef _WIN32

			for (int n = 0; n < fdRead.fd_count; n++)
			{
				auto iter = _clients.find(fdRead.fd_array[n]);
				if (iter != _clients.end())
				{
					if (-1 == RecvData(iter->second))
					{
						if (_pNetEvent)
							_pNetEvent->OnLeave(iter->second);
						_clients_change = true;
						_clients.erase(iter->first);
					}
				}
				else
				{
					std::cerr << "Oh,shit! what's error?" << std::endl;
				}
			}

#else
			std::vector<ClientSocket*>temp;
			for (auto iter : _clients)
			{
				if (FD_ISSET(iter.second->sockfd(), &fdRead))
				{
					if (-1 == RecvData(iter.second))
					{
						if (_pNetEvent)
							_pNetEvent->OnLeave(iter.second);
						_clients_change = false;
						temp.push_back(iter.second);
					}
				}
			}
			for (auto pClient : temp)
			{
				_clients.erase(pClient->sockfd());
				delete pClient;
			}

#endif
		}
		return false;
	}

	char _szRecv[RECV_BUFF_SZIE]{};
	int RecvData(ClientSocket* pClient)
	{
		int nLen = (int)recv(pClient->sockfd(), _szRecv, RECV_BUFF_SZIE, 0);
		// 触发事件 【pClient】 对象
		_pNetEvent->OnNetRecv(pClient);
		if (nLen <= 0)
		{
			//printf("Client <socket=%d> was exit. Count<%d>\n", (int)pClient->sockfd(), (int)_clients.size());
			return -1;
		}

		memcpy(pClient->msgBuf() + pClient->getLastPos(), _szRecv, nLen);

		pClient->setLastPos(pClient->getLastPos() + nLen);

		while (pClient->getLastPos() >= sizeof(DataHeader))
		{
			DataHeader* header = (DataHeader*)pClient->msgBuf();

			if (pClient->getLastPos() >= header->dataLength)
			{
				int nSize = pClient->getLastPos() - header->dataLength;
				OnNetMsg(pClient, header);
				memcpy(pClient->msgBuf(), pClient->msgBuf() + header->dataLength, nSize);
				pClient->setLastPos(nSize);
			}
			else
			{
				break;
			}
		}
		return 0;
	}

	virtual void OnNetMsg(ClientSocket* pClient, DataHeader* header)
	{
		// 触发网络事件.
		_pNetEvent->OnNetMsg(pClient, header);
	}

	void addClient(ClientSocket* pClient)
	{
		std::lock_guard<std::mutex>lock(_mutex);
		//_mutex.lock();
		_clientsBuff.push_back(pClient);
		//_mutex.unlock();
	}

	void CellsrvStart()
	{
		_pThread = new std::thread(std::mem_fun(&CellServer::OnRun), this);
	}

	size_t getClientCount()
	{
		return _clients.size() + _clientsBuff.size();
	}
};

class EasyTcpServer :public INetEvent
{
private:
	SOCKET _sock;
	// 消息处理对象 初始化启动线程
	std::vector<CellServer*> _cellServers;
	CELLTimestamp _tTime;

protected:
	std::atomic_int _recvCount;
	std::atomic_int _msgCount;
	std::atomic_int _clientCount;

public:

	EasyTcpServer()
	{
		_sock = INVALID_SOCKET;
		_recvCount = 0;
		_clientCount = 0;
		_msgCount = 0;
	}
	virtual ~EasyTcpServer()
	{
		Close();
	}

	SOCKET InitSocket()
	{
#ifdef _WIN32
		WORD ver = MAKEWORD(2, 2);
		WSADATA dat;
		WSAStartup(ver, &dat);
#endif
		if (INVALID_SOCKET != _sock)
		{
			printf("<socket=%d>old connection was disconneted.\n", (int)_sock);
			Close();
		}
		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock)
		{
			printf("Error, Create socket fail...\n");
		}
		else
		{
			printf("<socket=%d> was created...\n", (int)_sock);
		}
		return _sock;
	}

	int Bind(const char* ip, unsigned short port)
	{
		if (INVALID_SOCKET == _sock)
		{
			InitSocket();
		}
		sockaddr_in _sin{};
		_sin.sin_family = AF_INET;
		_sin.sin_port = htons(port);

#ifdef _WIN32
		if (ip)
		{
			_sin.sin_addr.S_un.S_addr = inet_addr(ip);
		}
		else
		{
			_sin.sin_addr.S_un.S_addr = INADDR_ANY;
		}
#else
		if (ip)
		{
			_sin.sin_addr.s_addr = inet_addr(ip);
		}
		else
		{
			_sin.sin_addr.s_addr = INADDR_ANY;
		}
#endif
		int ret = bind(_sock, (sockaddr*)&_sin, sizeof(_sin));
		if (SOCKET_ERROR == ret)
		{
			printf("Error, bind port<%d> fail...\n", port);
		}
		else
		{
			printf("Port<%d> bind sucess...\n", port);
		}
		return ret;
	}

	int Listen(int n)
	{
		int ret = listen(_sock, n);
		if (ret == SOCKET_ERROR)
		{
			printf("<socket=%d> listen error...\n", (int)_sock);
		}
		else
		{
			printf("<socket=%d> listen, wait for client connect...\n", (int)_sock);
		}
		return ret;
	}

	SOCKET Accept()
	{
		sockaddr_in clientAddr{};
		int nAddrLen = sizeof(sockaddr_in);
		SOCKET csock = INVALID_SOCKET;
#ifdef _WIN32
		csock = accept(_sock, (sockaddr*)&clientAddr, &nAddrLen);
#else
		csock = accept(_sock, (sockaddr*)&clientAddr, (socklen_t*)&nAddrLen);

#endif
		if (INVALID_SOCKET == csock)
		{
			printf("socket=<%d> error, invalid SOCKET...\n", (int)csock);
		}
		else
		{
			addClientToCellServer(new ClientSocket(csock));
			//client ip: inet_ntoa(clientAddr.sin_addr);
		}
		return csock;
	}

	void addClientToCellServer(ClientSocket* pClient)
	{
		auto pMinServer = _cellServers[0];
		for (auto pCellServer : _cellServers)
		{
			if (pMinServer->getClientCount() > pCellServer->getClientCount())
			{
				pMinServer = pCellServer;
			}
		}
		pMinServer->addClient(pClient);
		OnNetJoin(pClient);
	}

	void Start(int nCellServer)
	{
		for (int n = 0; n < nCellServer; n++)
		{
			auto ser = new CellServer(_sock);
			_cellServers.push_back(ser);
			// 注册网络事件接受对象
			ser->setEventObj(this);
			// 启动消息线程
			ser->CellsrvStart();
		}
	}

	void Close()
	{
		if (_sock != INVALID_SOCKET)
		{
#ifdef _WIN32
			closesocket(_sock);
			WSACleanup();
#else
			close(_sock);
#endif
		}
	}

	bool isRun()
	{
		return _sock != INVALID_SOCKET;
	}

	bool OnRun()
	{
		if (isRun())
		{
			Time4msg();

			fd_set fdRead;

			FD_ZERO(&fdRead);

			FD_SET(_sock, &fdRead);

			timeval tv{ 0,10 };
			int ret = (int)select(_sock + 1, &fdRead, nullptr, nullptr, &tv);

			if (ret < 0)
			{
				printf("Accept::select done.\n");
				Close();
				return false;
			}

			if (FD_ISSET(_sock, &fdRead))
			{
				FD_CLR(_sock, &fdRead);
				Accept();
				return true;
			}
			return true;
		}
		return false;
	}

	void Time4msg()
	{
		auto t1 = _tTime.getElapsedSecond();
		if (t1 >= 1.0)
		{
			printf("thread<%d>,time<%lf>,listensocket<%d>,client<%d>,receive_package_Count<%d>,request_msg<%d>\n", (int)_cellServers.size(), t1, (int)_sock, (int)_clientCount, (int)(_recvCount / t1), (int)(_msgCount / t1));
			_recvCount = 0;
			_msgCount = 0;
			_tTime.update();
		}
	}

	virtual void OnNetJoin(ClientSocket* pClient)
	{
		_clientCount++;
	}

	virtual void OnLeave(ClientSocket* pClient)
	{
		_clientCount--;
	}

	virtual void OnNetMsg(SOCKET cSock, DataHeader* header)
	{
		_msgCount++;
	}

	virtual void OnNetRecv(ClientSocket* pClient)
	{
		_recvCount++;
	}
};

class MyServer :public EasyTcpServer
{
private:
	std::map<SOCKET, ClientSocket*>_clientMap;
public:
	virtual void OnNetJoin(ClientSocket* pClient)
	{
		_clientCount++;
		_clientMap.insert(std::pair<SOCKET, ClientSocket*>(pClient->sockfd(), pClient));
		//printf("client<%d> join.\n", pClient->sockfd());
	}

	virtual void OnNetLeave(ClientSocket* pClient)
	{
		_clientCount--;
		if (_clientMap.count(pClient->sockfd()))
			_clientMap.erase(pClient->sockfd());
		//printf("client<%d> leave.\n", (int)pClient->sockfd());
	}

	virtual void OnNetMsg(ClientSocket* pClient, DataHeader* header)
	{
		_msgCount++;
		switch (header->cmd)
		{
		case CMD_LOGIN:
		{
			Login *login = (Login*)header;
			//printf("收到客户端<socket=%d>请求：CMD_LOGIN，数据长度：%d, userName=%s passWord=%s", (int)cSock, login->dataLength, login->userName, login->PassWord);
			LoginResult ret;
			SendData(pClient, &ret);
		}
		break;
		case CMD_LOGOUT:
		{
			Logout* logout = (Logout*)header;
			//printf("收到客户端<socket=%d>请求：CMD_LOGOUT，数据长度：%d, userName=%s", (int)cSock, login->dataLength, login->userName);
			LogoutResult ret;
			SendData(pClient, &ret);
		}
		break;

		default:
		{
			printf("<socket=%d>Error infomation, dataLength: %d\n", (int)pClient->sockfd(), header->dataLength);
		}
		break;
		}
	}

	virtual void OnNetRecv(ClientSocket* pClient)
	{
		_recvCount++;
	}

	int SendData(ClientSocket* pClient, DataHeader* header)
	{
		if (pClient->sockfd() && header)
		{
			return send(pClient->sockfd(), (const char*)header, header->dataLength, 0);
		}
		return SOCKET_ERROR;
	}

	void SendDataToAll(DataHeader* header)
	{
		for (auto _clientMap_iter = _clientMap.begin(); _clientMap_iter != _clientMap.end(); _clientMap_iter++)
		{
			SendData(_clientMap_iter->second, header);
		}
	}
};

#endif