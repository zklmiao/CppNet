#ifndef _CELLSERVER_HPP_
#define _CELLSERVER_HPP_

#include "PublicLib.hpp"
#include "INETEVENT.hpp"
#include "ClientSocket.hpp"
#include "CELLSemaphore.hpp"

#include <vector>
#include <map>

//网络消息接收处理服务类
class CellServer
{
public:
	CellServer(int id)
	{
		_id = id;
		_pNetEvent = nullptr;
		_taskServer._serverId = id;
	}

	~CellServer()
	{
		CellLog::Info("CellServer%d.~CellServer exit begin\n", _id);
		Close();
		CellLog::Info("CellServer%d.~CellServer exit end\n", _id);
	}

	void setEventObj(INetEvent* event)
	{
		_pNetEvent = event;
	}

	//关闭Socket
	void Close()
	{
		CellLog::Info("CellServer%d.Close begin\n", _id);
		_taskServer.Close();
		_thread.Close();
		CellLog::Info("CellServer%d.Close end\n", _id);
	}

	//处理网络消息
	void OnRun(CellThread* pThread)
	{
		while (pThread->isRun())
		{
			if (!_clientsBuff.empty())
			{//从缓冲队列里取出客户数据
				std::lock_guard<std::mutex> lock(_mutex);
				for (auto pClient : _clientsBuff)
				{
					_clients[pClient->sockfd()] = pClient;
					pClient->serverId = _id;
					if (_pNetEvent)
						_pNetEvent->OnNetJoin(pClient);
				}
				_clientsBuff.clear();
				_clients_change = true;
			}

			//如果没有需要处理的客户端，就跳过
			if (_clients.empty())
			{
				CellThread::Sleep(1);
				//旧的时间戳
				_oldTime = CELLTime::getNowInMillisec();
				continue;
			}

			CheckTime();

			//伯克利套接字 BSD socket
			//描述符（socket） 集合
			fd_set fdRead;
			fd_set fdWrite;
			//fd_set fdExc;

			if (_clients_change)
			{
				_clients_change = false;
				//清理集合
				FD_ZERO(&fdRead);
				//将描述符（socket）加入集合
				_maxSock = _clients.begin()->second->sockfd();
				for (auto iter : _clients)
				{
					FD_SET(iter.second->sockfd(), &fdRead);
					if (_maxSock < iter.second->sockfd())
					{
						_maxSock = iter.second->sockfd();
					}
				}
				memcpy(&_fdRead_bak, &fdRead, sizeof(fd_set));
			}
			else {
				memcpy(&fdRead, &_fdRead_bak, sizeof(fd_set));
			}

			bool bNeedWrite = false;
			FD_ZERO(&fdWrite);
			for (auto iter : _clients)
			{	//需要写数据的客户端,才加入fd_set检测是否可写
				if (iter.second->needWrite())
				{
					bNeedWrite = true;
					FD_SET(iter.second->sockfd(), &fdWrite);
				}
			}

			//memcpy(&fdWrite, &_fdRead_bak, sizeof(fd_set));
			//memcpy(&fdExc, &_fdRead_bak, sizeof(fd_set));

			///nfds 是一个整数值 是指fd_set集合中所有描述符(socket)的范围，而不是数量
			///既是所有文件描述符最大值+1 在Windows中这个参数可以写0
			timeval t{ 0,1 };
			int ret = 0;
			if (bNeedWrite)
			{
				ret = select(_maxSock + 1, &fdRead, &fdWrite, nullptr, &t);
			}
			else {
				ret = select(_maxSock + 1, &fdRead, nullptr, nullptr, &t);
			}
			if (ret < 0)
			{
				CellLog::Info("CellServer%d.OnRun.select Error exit\n", _id);
				pThread->Exit();
				break;
			}
			else if (ret == 0)
			{
				continue;
			}
			ReadData(fdRead);
			WriteData(fdWrite);
			//WriteData(fdExc);
			//CELLLog_Info("CELLServer%d.OnRun.select: fdRead=%d,fdWrite=%d", _id, fdRead.fd_count, fdWrite.fd_count);
			//if (fdExc.fd_count > 0)
			//{
			//	CELLLog_Info("###fdExc=%d", fdExc.fd_count);
			//}
		}
		CellLog::Info("CellServer%d.OnRun exit\n", _id);
	}

	void CheckTime()
	{
		//当前时间戳
		auto nowTime = CELLTime::getNowInMillisec();
		auto dt = nowTime - _oldTime;
		_oldTime = nowTime;

		for (auto iter = _clients.begin(); iter != _clients.end(); )
		{
			//心跳检测
			if (iter->second->checkHeart(dt))
			{
				if (_pNetEvent)
					_pNetEvent->OnLeave(iter->second);
				_clients_change = true;
				delete iter->second;
				auto iterOld = iter;
				iter++;
				_clients.erase(iterOld);
				continue;
			}

			////定时发送检测
			//iter->second->checkSend(dt);

			iter++;
		}
	}
	void OnClientLeave(ClientSocket* pClient)
	{
		if (_pNetEvent)
			_pNetEvent->OnLeave(pClient);
		_clients_change = true;
		delete pClient;
	}

	void WriteData(fd_set& fdWrite)
	{
#ifdef _WIN32
		for (int n = 0; n < fdWrite.fd_count; n++)
		{
			auto iter = _clients.find(fdWrite.fd_array[n]);
			if (iter != _clients.end())
			{
				if (-1 == iter->second->SendDataReal())
				{
					OnClientLeave(iter->second);
					_clients.erase(iter);
				}
			}
		}
#else
		for (auto iter = _clients.begin(); iter != _clients.end(); )
		{
			if (iter->second->needWrite() && FD_ISSET(iter->second->sockfd(), &fdWrite))
			{
				if (-1 == iter->second->SendDataReal())
				{
					OnClientLeave(iter->second);
					auto iterOld = iter;
					iter++;
					_clients.erase(iterOld);
					continue;
				}
				}
			iter++;
			}
#endif
		}

	void ReadData(fd_set& fdRead)
	{
#ifdef _WIN32
		for (int n = 0; n < fdRead.fd_count; n++)
		{
			auto iter = _clients.find(fdRead.fd_array[n]);
			if (iter != _clients.end())
			{
				if (-1 == RecvData(iter->second))
				{
					OnClientLeave(iter->second);
					_clients.erase(iter);
				}
			}
		}
#else
		for (auto iter = _clients.begin(); iter != _clients.end(); )
		{
			if (FD_ISSET(iter->second->sockfd(), &fdRead))
			{
				if (-1 == RecvData(iter->second))
				{
					OnClientLeave(iter->second);
					auto iterOld = iter;
					iter++;
					_clients.erase(iterOld);
					continue;
				}
			}
			iter++;
			}
#endif
		}

	//接收数据 处理粘包 拆分包
	int RecvData(ClientSocket* pClient)
	{
		//接收客户端数据
		int nLen = pClient->RecvData();
		if (nLen <= 0)
		{
			return -1;
		}
		//触发<接收到网络数据>事件
		_pNetEvent->OnNetRecv(pClient);
		//循环 判断是否有消息需要处理
		while (pClient->hasMsg())
		{
			//处理网络消息
			OnNetMsg(pClient, pClient->front_msg());
			//移除消息队列（缓冲区）最前的一条数据
			pClient->pop_front_msg();
		}
		return 0;
	}

	//响应网络消息
	virtual void OnNetMsg(ClientSocket* pClient, netmsg_DataHeader* header)
	{
		_pNetEvent->OnNetMsg(this, pClient, header);
	}

	void addClient(ClientSocket* pClient)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		//_mutex.lock();
		_clientsBuff.push_back(pClient);
		//_mutex.unlock();
	}

	void Start()
	{
		_taskServer.Start();
		_thread.Start(
			//onCreate
			nullptr,
			//onRun
			[this](CellThread* pThread) {
			OnRun(pThread);
		},
			//onDestory
			[this](CellThread* pThread) {
			ClearClients();
		}
		);
	}

	size_t getClientCount()
	{
		return _clients.size() + _clientsBuff.size();
	}

	//void addSendTask(CELLClient* pClient, netmsg_DataHeader* header)
	//{
	//	_taskServer.addTask([pClient, header]() {
	//		pClient->SendData(header);
	//		delete header;
	//	});
	//}
private:
	void ClearClients()
	{
		for (auto iter : _clients)
		{
			delete iter.second;
		}
		_clients.clear();

		for (auto iter : _clientsBuff)
		{
			delete iter;
		}
		_clientsBuff.clear();
	}
private:
	//正式客户队列
	std::map<SOCKET, ClientSocket*> _clients;
	//缓冲客户队列
	std::vector<ClientSocket*> _clientsBuff;
	//缓冲队列的锁
	std::mutex _mutex;
	//网络事件对象
	INetEvent* _pNetEvent;
	//
	CellTaskServer _taskServer;
	//备份客户socket fd_set
	fd_set _fdRead_bak;
	//
	SOCKET _maxSock;
	//旧的时间戳
	time_t _oldTime = CELLTime::getNowInMillisec();
	//
	CellThread _thread;
	//
	int _id = -1;
	//客户列表是否有变化
	bool _clients_change = true;
	};
#endif