#include "connection_pool.h"
#include "connection.h"

#include <engine/console.h>

// helper struct to hold thread data
struct CSqlExecData
{
	CSqlExecData(
		CDbConnectionPool::FRead pFunc,
		std::unique_ptr<const ISqlData> pThreadData,
		const char *pName);
	CSqlExecData(
		CDbConnectionPool::FWrite pFunc,
		std::unique_ptr<const ISqlData> pThreadData,
		const char *pName);
	~CSqlExecData() {}

	enum
	{
		READ_ACCESS,
		WRITE_ACCESS,
	} m_Mode;
	union
	{
		CDbConnectionPool::FRead m_pReadFunc;
		CDbConnectionPool::FWrite m_pWriteFunc;
	} m_Ptr;

	std::unique_ptr<const ISqlData> m_pThreadData;
	const char *m_pName;
};

CSqlExecData::CSqlExecData(
	CDbConnectionPool::FRead pFunc,
	std::unique_ptr<const ISqlData> pThreadData,
	const char *pName) :
	m_Mode(READ_ACCESS),
	m_pThreadData(std::move(pThreadData)),
	m_pName(pName)
{
	m_Ptr.m_pReadFunc = pFunc;
}

CSqlExecData::CSqlExecData(
	CDbConnectionPool::FWrite pFunc,
	std::unique_ptr<const ISqlData> pThreadData,
	const char *pName) :
	m_Mode(WRITE_ACCESS),
	m_pThreadData(std::move(pThreadData)),
	m_pName(pName)
{
	m_Ptr.m_pWriteFunc = pFunc;
}

CDbConnectionPool::CDbConnectionPool() :
	m_NumElem(),
	FirstElem(0),
	LastElem(0)
{
	thread_init_and_detach(CDbConnectionPool::Worker, this, "database worker thread");
}

CDbConnectionPool::~CDbConnectionPool()
{
}

void CDbConnectionPool::Print(IConsole *pConsole, Mode DatabaseMode)
{
	const char *ModeDesc[] = {"Read", "Write", "WriteBackup"};
	for(unsigned int i = 0; i < m_aapDbConnections[DatabaseMode].size(); i++)
	{
		m_aapDbConnections[DatabaseMode][i]->Print(pConsole, ModeDesc[DatabaseMode]);
	}
}

void CDbConnectionPool::RegisterDatabase(std::unique_ptr<IDbConnection> pDatabase, Mode DatabaseMode)
{
	if(DatabaseMode < 0 || NUM_MODES <= DatabaseMode)
		return;
	m_aapDbConnections[DatabaseMode].push_back(std::move(pDatabase));
}

void CDbConnectionPool::Execute(
	FRead pFunc,
	std::unique_ptr<const ISqlData> pThreadData,
	const char *pName)
{
	m_aTasks[FirstElem++].reset(new CSqlExecData(pFunc, std::move(pThreadData), pName));
	FirstElem %= sizeof(m_aTasks) / sizeof(m_aTasks[0]);
	m_NumElem.Signal();
}

void CDbConnectionPool::ExecuteWrite(
	FWrite pFunc,
	std::unique_ptr<const ISqlData> pThreadData,
	const char *pName)
{
	m_aTasks[FirstElem++].reset(new CSqlExecData(pFunc, std::move(pThreadData), pName));
	FirstElem %= sizeof(m_aTasks) / sizeof(m_aTasks[0]);
	m_NumElem.Signal();
}

void CDbConnectionPool::OnShutdown()
{
	m_Shutdown.store(true);
	m_NumElem.Signal();
	int i = 0;
	while(m_Shutdown.load())
	{
		if(i > 600)
		{
			dbg_msg("sql", "Waited 60 seconds for score-threads to complete, quitting anyway");
			break;
		}

		// print a log about every two seconds
		if(i % 20 == 0)
			dbg_msg("sql", "Waiting for score-threads to complete (%ds)", i / 10);
		++i;
		thread_sleep(100000);
	}
}

void CDbConnectionPool::Worker(void *pUser)
{
	CDbConnectionPool *pThis = (CDbConnectionPool *)pUser;
	pThis->Worker();
}

void CDbConnectionPool::Worker()
{
	// remember last working server and try to connect to it first
	int ReadServer = 0;
	int WriteServer = 0;
	while(1)
	{
		m_NumElem.Wait();
		auto pThreadData = std::move(m_aTasks[LastElem++]);
		// work through all database jobs after OnShutdown is called before exiting the thread
		if(pThreadData == nullptr)
		{
			m_Shutdown.store(false);
			return;
		}
		LastElem %= sizeof(m_aTasks) / sizeof(m_aTasks[0]);
		bool Success = false;
		switch(pThreadData->m_Mode)
		{
		case CSqlExecData::READ_ACCESS:
		{
			for(int i = 0; i < (int)m_aapDbConnections[Mode::READ].size(); i++)
			{
				int CurServer = (ReadServer + i) % (int)m_aapDbConnections[Mode::READ].size();
				if(ExecSqlFunc(m_aapDbConnections[Mode::READ][CurServer].get(), pThreadData.get(), false))
				{
					ReadServer = CurServer;
					dbg_msg("sql", "%s done on read database %d", pThreadData->m_pName, CurServer);
					Success = true;
					break;
				}
			}
		}
		break;
		case CSqlExecData::WRITE_ACCESS:
		{
			for(int i = 0; i < (int)m_aapDbConnections[Mode::WRITE].size(); i++)
			{
				int CurServer = (WriteServer + i) % (int)m_aapDbConnections[Mode::WRITE].size();
				if(ExecSqlFunc(m_aapDbConnections[Mode::WRITE][i].get(), pThreadData.get(), false))
				{
					WriteServer = CurServer;
					dbg_msg("sql", "%s done on write database %d", pThreadData->m_pName, CurServer);
					Success = true;
					break;
				}
			}
			if(!Success)
			{
				for(int i = 0; i < (int)m_aapDbConnections[Mode::WRITE_BACKUP].size(); i++)
				{
					if(ExecSqlFunc(m_aapDbConnections[Mode::WRITE_BACKUP][i].get(), pThreadData.get(), true))
					{
						dbg_msg("sql", "%s done on write backup database %d", pThreadData->m_pName, i);
						Success = true;
						break;
					}
				}
			}
		}
		break;
		}
		if(!Success)
			dbg_msg("sql", "%s failed on all databases", pThreadData->m_pName);
		if(pThreadData->m_pThreadData->m_pResult != nullptr)
		{
			pThreadData->m_pThreadData->m_pResult->m_Success = Success;
			pThreadData->m_pThreadData->m_pResult->m_Completed.store(true);
		}
	}
}

bool CDbConnectionPool::ExecSqlFunc(IDbConnection *pConnection, CSqlExecData *pData, bool Failure)
{
	char aError[256] = "error message not initialized";
	if(pConnection->Connect(aError, sizeof(aError)))
	{
		dbg_msg("sql", "failed connecting to db: %s", aError);
		return false;
	}
	bool Success = false;
	switch(pData->m_Mode)
	{
	case CSqlExecData::READ_ACCESS:
		Success = !pData->m_Ptr.m_pReadFunc(pConnection, pData->m_pThreadData.get(), aError, sizeof(aError));
		break;
	case CSqlExecData::WRITE_ACCESS:
		Success = !pData->m_Ptr.m_pWriteFunc(pConnection, pData->m_pThreadData.get(), Failure, aError, sizeof(aError));
		break;
	}
	pConnection->Disconnect();
	if(!Success)
	{
		dbg_msg("sql", "%s failed: %s", pData->m_pName, aError);
	}
	return Success;
}
