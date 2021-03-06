#ifndef _EasyTcpServer2_3_hpp_
#define _EasyTcpServer2_3_hpp_

#include "PublicLib.hpp"
#include "ClientSocket.hpp"
#include "CellServer.hpp"
#include "INETEVENT.hpp"
#include "CellNetWork.hpp"
#include "CellConfig.hpp"

#include <thread>
#include <mutex>
#include <atomic>

class EasyTcpServer :public INetEvent
{
private:

	CellThread _cellThread;
	// 消息处理对象 初始化启动线程
	std::vector<CellServer*> _cellServers;
	CELLTimestamp _tTime;
	SOCKET _sock;

	int _nSendBuffSize;
	int _nRecvBuffSize;
	int _nMaxClient;

protected:
	std::atomic_int _recvCount;
	std::atomic_int _msgCount;
	std::atomic_int _clientCount;

public:

	EasyTcpServer()
	{
		_sock = INVALID_SOCKET;
		_recvCount = 0;
		_msgCount = 0;
		_clientCount = 0;

		_nSendBuffSize = CellConfig::Instance().getInt("nSendBuffSize", SEND_BUFF_SZIE);
		_nRecvBuffSize = CellConfig::Instance().getInt("nRecvBuffSize", RECV_BUFF_SZIE);
		_nMaxClient = CellConfig::Instance().getInt("nMaxClient", FD_SETSIZE);
	}
	virtual ~EasyTcpServer()
	{
		Close();
	}

	SOCKET InitSocket()
	{
		CellNetWork::Init();

#ifdef _WIN32	
#else
		int opt = 1;
		unsigned int len = sizeof(opt);
		setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, &opt, len);
		setsockopt(_sock, SOL_SOCKET, SO_KEEPALIVE, &opt, len);
#endif

		if (INVALID_SOCKET != _sock)
		{
			CellLog_Debug("<socket=%d>old connection was disconneted.", (int)_sock);
			Close();
		}
		_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock)
		{
			CellLog_Debug("Error, Create socket fail...");
		}
		else
		{
			CellLog_Debug("<socket=%d> was created...", (int)_sock);
		}
		return _sock;
	}

	int Bind(const char* ip, unsigned short port)
	{
		//if (INVALID_SOCKET == _sock)
		//{
		//	InitSocket();
		//}
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
			CellLog_Debug("Error, bind port<%d> fail...", port);
		}
		else
		{
			CellLog_Debug("Port<%d> bind sucess...", port);
		}
		return ret;
	}

	int Listen(int n)
	{
		int ret = listen(_sock, n);
		if (SOCKET_ERROR == ret)
		{
			CellLog_Debug("<socket=%d> listen error...", (int)_sock);
		}
		else
		{
			CellLog_Debug("<socket=%d> listen, wait for client connect...", (int)_sock);
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
			CellLog_Debug("socket=<%d> error, invalid SOCKET...error code:%d %s.", (int)csock,errno,strerror(errno));
		}
		else
		{
			if (_clientCount < _nMaxClient)
			{
				addClientToCellServer(new ClientSocket(csock,_nSendBuffSize,_nRecvBuffSize));
			}
			else
			{
#ifdef _WIN32
				closesocket(csock);
#else
				close(csock);
#endif // _WIN32
				CellLog_Warring("Accept to nMaxClient");
			}
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
	}

	void Start(int nCellServer)
	{
		for (int n = 0; n < nCellServer; n++)
		{
			auto ser = new CellServer(n + 1);
			_cellServers.push_back(ser);
			// 注册网络事件接受对象
			ser->setEventObj(this);
			// 启动消息线程
			ser->CellsrvStart();
		}
		_cellThread.Start(nullptr,
			[this](CellThread* pThread) {OnRun(pThread); });
	}

	void Close()
	{
		CellLog_Debug("Server2.2 closed.code:1");
		_cellThread.Close();
		if (_sock != INVALID_SOCKET)
		{
			for (auto s : _cellServers)
			{
				delete s;
			}
			_cellServers.clear();
#ifdef _WIN32
			closesocket(_sock);
#else
			close(_sock);
#endif
			_sock = INVALID_SOCKET;
		}
		CellLog_Debug("Server2.2 closed.code:2");

	}

	virtual void OnNetJoin(ClientSocket* pClient)
	{
		_clientCount++;
	}

	virtual void OnLeave(ClientSocket* pClient)
	{
		_clientCount--;
	}

	virtual void OnNetMsg(CellServer* pCellServer, SOCKET cSock, netmsg_DataHeader* header)
	{
		_msgCount++;
	}

	virtual void OnNetRecv(ClientSocket* pClient)
	{
		_recvCount++;
	}

private:
	void OnRun(CellThread* pThread)
	{
		while (pThread->isRun())
		{
			Time4msg();

			fd_set fdRead;

			FD_ZERO(&fdRead);

			FD_SET(_sock, &fdRead);

			timeval tv{ 0,1 };
			int ret = (int)select(_sock + 1, &fdRead, nullptr, nullptr, &tv);

			if (ret < 0)
			{
				CellLog_Debug("EasyTcpServer.accept.select exit.");
				pThread->Exit();
				break;
			}

			if (FD_ISSET(_sock, &fdRead))
			{
				FD_CLR(_sock, &fdRead);
				Accept();
			}
		}
	}

	void Time4msg()
	{
		auto t1 = _tTime.getElapsedSecond();
		if (t1 >= 1.0)
		{
			CellLog_Debug("thread<%d>,time<%lf>,listen<%d>,client<%d>,receive<%d>,msg<%d>", (int)_cellServers.size(), t1, (int)_sock, (int)_clientCount, (int)_recvCount, (int)_msgCount);
			_recvCount = 0;
			_msgCount = 0;
			_tTime.update();
		}
	}
};

#endif