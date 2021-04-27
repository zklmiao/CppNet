#ifndef _MYSERVER_HPP_
#define _MYSERVER_HPP_

#include "EasyTcpServer1_8_2.hpp"

//应用层
class MyServer :public EasyTcpServer
{
private:
	std::map<SOCKET, ClientSocket*>_clientMap;
public:
	virtual void OnNetJoin(ClientSocket* pClient)
	{
		EasyTcpServer::OnNetJoin(pClient);
		_clientMap.insert(std::pair<SOCKET, ClientSocket*>(pClient->sockfd(), pClient));
		//printf("client<%d> join.\n", pClient->sockfd());
	}

	virtual void OnNetLeave(ClientSocket* pClient)
	{
		EasyTcpServer::OnLeave(pClient);
		if (_clientMap.count(pClient->sockfd()))
			_clientMap.erase(pClient->sockfd());
		//printf("client<%d> leave.\n", (int)pClient->sockfd());
	}

	virtual void OnNetMsg(CellServer* pCellServer, ClientSocket* pClient, netmsg_DataHeader* header)
	{
		EasyTcpServer::OnNetMsg(pCellServer, pClient->sockfd(), header);
		switch (header->cmd)
		{
		case CMD_LOGIN:
		{
			netmsg_Login *login = (netmsg_Login*)header;
			//printf("收到客户端<socket=%d>请求：CMD_LOGIN，数据长度：%d, userName=%s passWord=%s", (int)cSock, login->dataLength, login->userName, login->PassWord);
			//LoginResult ret;
			//SendData(pClient, &ret);
			// 不安全写法 将业务层暴露给用户(应用层)
			netmsg_LoginR *ret = new netmsg_LoginR();

			pCellServer->addSendTask(pClient, ret);
		}
		break;
		case CMD_LOGOUT:
		{
			netmsg_Logout* logout = (netmsg_Logout*)header;
			//printf("收到客户端<socket=%d>请求：CMD_LOGOUT，数据长度：%d, userName=%s", (int)cSock, login->dataLength, login->userName);
			netmsg_LogoutR ret;
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

	int SendData(ClientSocket* pClient, netmsg_DataHeader* header)
	{
		if (pClient->sockfd() && header)
		{
			return send(pClient->sockfd(), (const char*)header, header->dataLength, 0);
		}
		return SOCKET_ERROR;
	}

	void SendDataToAll(netmsg_DataHeader* header)
	{
		for (auto _clientMap_iter = _clientMap.begin(); _clientMap_iter != _clientMap.end(); _clientMap_iter++)
		{
			SendData(_clientMap_iter->second, header);
		}
	}
};
#endif