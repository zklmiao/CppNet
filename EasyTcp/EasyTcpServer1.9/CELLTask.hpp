#ifndef _CELL_TASK_H_
#define _CELL_TASK_H_

#include <thread>
#include <mutex>
#include <list>
#include <functional>

#include "CellThread.hpp"

// 任务类[实现]
class CellTaskServer
{
public:
	int _serverId = -1;
private:
	typedef std::function<void()> CellTask;
private:
	// 任务队列
	std::list<CellTask> _tasks;
	// 任务队列缓冲区
	std::list<CellTask> _tasksBuff;
	std::mutex _mutex;
	CellThread _cellThread;
public:
	CellTaskServer()
	{
	}
	~CellTaskServer()
	{
	}
	//添加任务到缓冲区队列
	void addTask(CellTask task)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_tasksBuff.push_back(task);
	}
	// 启动服务
	void Start()
	{
		_cellThread.Start(
			nullptr, 
			[this](CellThread* pThread) {OnRun(pThread);});
	}

	void Close()
	{
		printf("CELLTask%d closed.code:1\n", _serverId);
		_cellThread.Close();
		printf("CELLTask%d closed.code:2\n", _serverId);
	}

protected:
	// 工作函数 从缓冲区取出任务事件放到任务队列中并顺序执行队列中的任务
	void OnRun(CellThread* pThread)
	{
		while (pThread->isRun())
		{
			if (!_tasksBuff.empty())
			{
				std::lock_guard<std::mutex> lock(_mutex);
				for (auto pTask : _tasksBuff)
				{
					_tasks.push_back(pTask);
				}
				_tasksBuff.clear();
			}
			if (_tasksBuff.empty())
			{
				std::chrono::milliseconds t(1);
				std::this_thread::sleep_for(t);
				continue;
			}
			// 处理任务
			for (auto pTask : _tasks)
			{
				pTask();
			}
			// 清空任务队列
			_tasks.clear();
		}
		printf("CellServer%d::OnRun() exit.code:%d(0 normal)\n", _serverId,_cellThread.isRun());
	}
};
#endif