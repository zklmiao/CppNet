#ifndef _CLIENT_SOCKET_HPP_
#define _CLIENT_SOCKET_HPP_

#define CLIENT_HEART_DEAD_TIME 60000 //5000 ms
// 超时发送时间
#define CLIENT_SEND_BUFF_TIME 200 // ms

#include "CellMsgBuffer.hpp"
#include "CellNetWork.hpp"

//客户端数据类型
class ClientSocket
{
public:
	int id = -1;
	int serverId = -1;
	int _nRecvMsgID = 1;
	int _nSendMsgID = 1;
public:
	ClientSocket(SOCKET sockfd = INVALID_SOCKET, int sendSize = SEND_BUFF_SZIE, int recvSize = RECV_BUFF_SZIE)
		:_sendBuff(sendSize),
		_recvBuff(recvSize)
	{
		static int n = 1;
		id = n++;
		_sockfd = sockfd;

		resetDTHeart();
		resetDTSend();
	}
	~ClientSocket()
	{
		//CellLog_Debug("s=%d CellClient%d closed.code:1",serverId, id);
		destory();
	}

	void destory()
	{
		if (INVALID_SOCKET != _sockfd)
		{
			CellNetWork::destorySocket(_sockfd);
			_sockfd = INVALID_SOCKET;
		}
	}

	SOCKET sockfd()
	{
		return _sockfd;
	}

	int RecvData()
	{
		return _recvBuff.read4socket(_sockfd);
	}

	bool hasMsg()
	{
		return _recvBuff.hasMsg();
	}

	netmsg_DataHeader* front_msg()
	{
		return (netmsg_DataHeader*)_recvBuff.data();
	}

	// 移除消息队列第一条
	void pop_front_msg()
	{
		if (hasMsg())
			_recvBuff.pop(front_msg()->dataLength);

	}

	bool needWrite()
	{
		return _sendBuff.needWrite();
	}

	int SendDataReal()
	{
		resetDTSend();
		return _sendBuff.write2socket(_sockfd);
	}

	//发送数据
	int SendData(netmsg_DataHeader* header)
	{
		return SendData((const char*)header, header->dataLength);
	}

	int SendData(const char* pData, int len)
	{
		if (_sendBuff.push(pData,len))
		{
			return len;
		}

		return SOCKET_ERROR;
	}

	void resetDTHeart()
	{
		_dtHeart = 0;
	}

	void resetDTSend()
	{
		_dtSend = 0;
	}

	// 心跳检测
	bool checkHeart(time_t dt)
	{
		_dtHeart += dt;
		if (_dtHeart >= CLIENT_HEART_DEAD_TIME)
		{
			//CellLog_Debug("checkHeart dead:s=%d,time=%d", _sockfd, _dtHeart);
			return true;
		}
		return false;
	}
	// 超时发送
	bool checkSend(time_t dt)
	{
		_dtSend += dt;
		if (_dtSend >= CLIENT_SEND_BUFF_TIME)
		{
			// 立即发送在发送缓冲区中的数据
			// 重置超时计数 _dtSend
			//CellLog_Debug("Send timeout.");
			SendDataReal();
			resetDTSend();
			return true;
		}
		return false;
	}
#ifdef CELL_USE_IOCP
	// IOCP recv
	inline IO_DATA_BASE* makeRecvIOData()
	{
		if (_isPostRecv)
		{
			//printf("Error\n");
			return nullptr;
		}
		_isPostRecv = true;
		return _recvBuff.makeRecvIOData(_sockfd);
	}
	void recv4IOCP(int nRecv)
	{
		if (!_isPostRecv);
			//printf("Error\n");
		_isPostRecv = false;
		_recvBuff.read4iocp(nRecv);
	}

	// IOCP send
	inline IO_DATA_BASE* makeSendIOData()
	{
		if (_isPostSend)
		{
			//printf("Error\n");
			return nullptr;
		}
		_isPostSend = true;
		return _sendBuff.makeSendIOData(_sockfd);
	}
	void send2IOCP(int nSend)
	{
		if (!_isPostSend);
		//printf("Error\n");
		_isPostSend = false;
		_sendBuff.wirte2iocp(nSend);
	}

	bool isPostIoAction()
	{
		return _isPostRecv || _isPostSend;
	}

#endif // CELL_USE_IOCP

private:
	// socket fd_set  file desc set
	SOCKET _sockfd;
	//接收消息缓冲区
	CellBuffer _recvBuff;
	//发送缓冲区
	CellBuffer _sendBuff;

	//心跳死亡计时
	time_t _dtHeart;
	// 发送超时时间
	time_t _dtSend;
	// 发送缓冲区溢满计数
	int _sendBuffFullCount = 0;

#ifdef CELL_USE_IOCP
	bool _isPostRecv = false;
	bool _isPostSend = false;
#endif

};

#endif // !_CLIENT_SOCKET_HPP_
