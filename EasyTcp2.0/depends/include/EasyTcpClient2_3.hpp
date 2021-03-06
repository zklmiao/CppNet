#ifndef _EasyTc_pClient2_3_hpp_
#define _EasyTc_pClient2_3_hpp_

#include "PublicLib.hpp"
#include "CellNetWork.hpp"
#include "ClientSocket.hpp"
#include "CellLog.hpp"

class EasyTcpClient
{
public:
	EasyTcpClient()
	{
		_isConnected = false;
	}

	virtual ~EasyTcpClient()
	{
		Close();
	}

	SOCKET InitSocket(int sendSize = SEND_BUFF_SZIE,int recvSize = RECV_BUFF_SZIE)
	{
		CellNetWork::Init();
		if (_pClient)
		{
			CellLog_Debug("<socket=%d>old connection was disconnected...", (int)_pClient->sockfd());
			Close();
		}
		SOCKET _sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (INVALID_SOCKET == _sock)
		{
			CellLog_Debug("Error, create socket<%d> fail...", (int)_sock);
		}
		else
		{
			_pClient = new ClientSocket(_sock,sendSize,recvSize);
			//CellLog_Debug("Create socket=<%d> sucess...", (int)_sock);
		}
		return _sock;
	}

	int Connect(const char* ip, unsigned short port)
	{
		if (!_pClient)
		{
			if (INVALID_SOCKET == InitSocket())
			{
				return SOCKET_ERROR;
			}
		}
		sockaddr_in _sin{};
		_sin.sin_family = AF_INET;
		_sin.sin_port = htons(port);
#ifdef _WIN32
		if (ip)
			_sin.sin_addr.S_un.S_addr = inet_addr(ip);
#else
		_sin.sin_addr.s_addr = inet_addr(ip);
#endif // _WIN32
		//CellLog_Debug("<socket=%d> connect to host<%s:%d>...", (int)_sock, ip, port);
		int ret = connect(_pClient->sockfd(), (sockaddr*)&_sin, sizeof(sockaddr_in));
		if (SOCKET_ERROR == ret)
		{
			CellLog_Debug("<socket=%d> error, connect to host<%s:%d> fail...", (int)_pClient->sockfd(), ip, port);
		}
		else
		{
			_isConnected = true;
			//CellLog_Debug("<socket=%d> connect to host<%s:%d> sucess.", (int)_sock, ip, port);
		}
		return ret;
	}

	void Close()
	{
		if (_pClient)
		{
			delete _pClient;
			_pClient = nullptr;
		}
		_isConnected = false;
	}

	bool OnRun(int microseconds = 1)
	{
		if (isRun())
		{
			SOCKET sock = _pClient->sockfd();
			fd_set fdReads;
			FD_ZERO(&fdReads);
			FD_SET(sock, &fdReads);

			fd_set fdWrite;
			FD_ZERO(&fdWrite);

			timeval tv = { 0,microseconds };
			int ret = 0;
			if (_pClient->needWrite())
			{
				FD_SET(sock, &fdWrite);
				ret = (int)select(sock + 1, &fdReads, &fdWrite, 0, &tv);
			}
			else
			{
				ret = (int)select(sock + 1, &fdReads, nullptr, nullptr, &tv);
			}
			
			if (ret < 0)
			{
				CellLog_Debug("<socket=%d>select done. terminal code:1", (int)sock);
				Close();
				return false;
			}
			if (FD_ISSET(sock, &fdReads))
			{
				//FD_CLR(_sock, &fdReads);
				if (-1 == RecvData(sock))
				{
					CellLog_Debug("<socket=%d>select done. terminal code:2", (int)sock);
					Close();
					return false;
				}
			}

			if (FD_ISSET(sock, &fdWrite))
			{
				//FD_CLR(_sock, &fdReads);
				if (-1 == _pClient->SendDataReal()) // 
				{
					Close();
					return false;
				}
			}

			return true;
		}
		return false;
	}

	bool isRun()
	{
		return _pClient && _isConnected;
	}

	int RecvData(SOCKET csock)
	{
		if (isRun())
		{
			// ?????????
			int nLen = _pClient->RecvData();
			// ??????????????????
			if (nLen > 0)
			{
				while (_pClient->hasMsg())
				{
					// ??????????????????????????????
					OnNetMsg(_pClient->front_msg());
					// ??????????????????????????? ??????????????????
					_pClient->pop_front_msg();
				}
			}
			return nLen;
		}
		return 0;
	}

	virtual void OnNetMsg(netmsg_DataHeader* header) = 0;
	
	int SendData(netmsg_DataHeader* header)
	{
		if (isRun())
			return _pClient->SendData(header);
		else return 0;
	}

	int SendData(const char* pData, int len)
	{
		if (isRun())
			return _pClient->SendData(pData, len);
		return 0;
	}

protected:
	ClientSocket* _pClient = nullptr;

private:
	bool _isConnected;
};

#endif