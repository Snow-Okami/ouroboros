// 2017-2018 Rotten Visions, LLC. https://www.rottenvisions.com
#include "dbmgr.h"
#include "interfaces_handler.h"
#include "buffered_dbtasks.h"
#include "db_interface/db_threadpool.h"
#include "db_interface/db_interface.h"
#include "thread/threadpool.h"
#include "thread/threadguard.h"
#include "server/serverconfig.h"
#include "network/endpoint.h"
#include "network/channel.h"
#include "network/bundle.h"

#include "baseapp/baseapp_interface.h"
#include "tools/interfaces/interfaces_interface.h"

namespace Ouroboros{

//-------------------------------------------------------------------------------------
InterfacesHandler* InterfacesHandlerFactory::create(std::string type)
{
	if(type.size() == 0 || type == "dbmgr")
	{
		return new InterfacesHandler_Dbmgr();
	}
	else if(type == "interfaces")
	{
		return new InterfacesHandler_Interfaces();
	}

	return NULL;
}

//-------------------------------------------------------------------------------------
InterfacesHandler::InterfacesHandler()
{
}

//-------------------------------------------------------------------------------------
InterfacesHandler::~InterfacesHandler()
{
}

//-------------------------------------------------------------------------------------
InterfacesHandler_Dbmgr::InterfacesHandler_Dbmgr() :
InterfacesHandler()
{
}

//-------------------------------------------------------------------------------------
InterfacesHandler_Dbmgr::~InterfacesHandler_Dbmgr()
{
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Dbmgr::createAccount(Network::Channel* pChannel, std::string& registerName,
										  std::string& password, std::string& datas, ACCOUNT_TYPE uatype)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(registerName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::createAccount: not found dbInterface({})!\n",
			dbInterfaceName));

		return false;
	}

	// If it is email, first check whether the account exists and then register it in the database
	if(uatype == ACCOUNT_TYPE_MAIL)
	{
		pThreadPool->addTask(new DBTaskCreateMailAccount(pChannel->addr(),
			registerName, registerName, password, datas, datas));

		return true;
	}

	pThreadPool->addTask(new DBTaskCreateAccount(pChannel->addr(),
		registerName, registerName, password, datas, datas));

	return true;
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::onCreateAccountCB(Ouroboros::MemoryStream& s)
{
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Dbmgr::loginAccount(Network::Channel* pChannel, std::string& loginName,
										 std::string& password, std::string& datas)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(loginName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::loginAccount: not found dbInterface({})!\n",
			dbInterfaceName));

		return false;
	}

	pThreadPool->addTask(new DBTaskAccountLogin(pChannel->addr(),
		loginName, loginName, password, SERVER_SUCCESS, datas, datas, true));

	return true;
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::onLoginAccountCB(Ouroboros::MemoryStream& s)
{
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::charge(Network::Channel* pChannel, Ouroboros::MemoryStream& s)
{
	INFO_MSG("InterfacesHandler_Dbmgr::charge: no implement!\n");
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::onChargeCB(Ouroboros::MemoryStream& s)
{
	INFO_MSG("InterfacesHandler_Dbmgr::onChargeCB: no implement!\n");
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::eraseClientReq(Network::Channel* pChannel, std::string& logkey)
{
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::accountActivate(Network::Channel* pChannel, std::string& scode)
{
	ENGINE_COMPONENT_INFO& dbcfg = g_ouroSrvConfig.getDBMgr();
	std::vector<DBInterfaceInfo>::iterator dbinfo_iter = dbcfg.dbInterfaceInfos.begin();
	for (; dbinfo_iter != dbcfg.dbInterfaceInfos.end(); ++dbinfo_iter)
	{
		std::string dbInterfaceName = dbinfo_iter->name;

		thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
		if (!pThreadPool)
		{
			ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::accountActivate: not found dbInterface({})!\n",
				dbInterfaceName));

			return;
		}

		pThreadPool->addTask(new DBTaskActivateAccount(pChannel->addr(), scode));
	}
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::accountReqResetPassword(Network::Channel* pChannel, std::string& accountName)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(accountName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::accountReqResetPassword: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskReqAccountResetPassword(pChannel->addr(), accountName));
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::accountResetPassword(Network::Channel* pChannel, std::string& accountName, std::string& newpassword, std::string& scode)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(accountName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::accountResetPassword: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskAccountResetPassword(pChannel->addr(), accountName, newpassword, scode));
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::accountReqBindMail(Network::Channel* pChannel, ENTITY_ID entityID, std::string& accountName,
											   std::string& password, std::string& email)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(accountName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::accountReqBindMail: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskReqAccountBindEmail(pChannel->addr(), entityID, accountName, password, email));
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::accountBindMail(Network::Channel* pChannel, std::string& username, std::string& scode)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(username);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::accountBindMail: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskAccountBindEmail(pChannel->addr(), username, scode));
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Dbmgr::accountNewPassword(Network::Channel* pChannel, ENTITY_ID entityID, std::string& accountName,
											   std::string& password, std::string& newpassword)
{
	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(accountName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Dbmgr::accountNewPassword: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskAccountNewPassword(pChannel->addr(), entityID, accountName, password, newpassword));
}

//-------------------------------------------------------------------------------------
InterfacesHandler_Interfaces::InterfacesHandler_Interfaces() :
InterfacesHandler_Dbmgr()
{
}

//-------------------------------------------------------------------------------------
InterfacesHandler_Interfaces::~InterfacesHandler_Interfaces()
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());
	if(pInterfacesChannel)
	{
		pInterfacesChannel->condemn();
	}

	pInterfacesChannel = NULL;
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Interfaces::createAccount(Network::Channel* pChannel, std::string& registerName,
											  std::string& password, std::string& datas, ACCOUNT_TYPE uatype)
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());
	OURO_ASSERT(pInterfacesChannel);

	if(pInterfacesChannel->isDestroyed())
	{
		if(!this->reconnect())
		{
			return false;
		}
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject();

	(*pBundle).newMessage(InterfacesInterface::reqCreateAccount);
	(*pBundle) << pChannel->componentID();

	uint8 accountType = uatype;
	(*pBundle) << registerName << password << accountType;
	(*pBundle).appendBlob(datas);
	pInterfacesChannel->send(pBundle);
	return true;
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::onCreateAccountCB(Ouroboros::MemoryStream& s)
{
	std::string registerName, accountName, password, postdatas, getdatas;
	COMPONENT_ID cid;
	SERVER_ERROR_CODE success = SERVER_ERR_OP_FAILED;

	s >> cid >> registerName >> accountName >> password >> success;
	s.readBlob(postdatas);
	s.readBlob(getdatas);

	if(success != SERVER_SUCCESS)
	{
		accountName = "";
	}

	Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(LOGINAPP_TYPE, cid);
	if(cinfos == NULL || cinfos->pChannel == NULL)
	{
		ERROR_MSG("InterfacesHandler_Interfaces::onCreateAccountCB: loginapp not found!\n");
		return;
	}

	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(accountName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Interfaces::onCreateAccountCB: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskCreateAccount(cinfos->pChannel->addr(),
		registerName, accountName, password, postdatas, getdatas));
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Interfaces::loginAccount(Network::Channel* pChannel, std::string& loginName,
											 std::string& password, std::string& datas)
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());
	OURO_ASSERT(pInterfacesChannel);

	if(pInterfacesChannel->isDestroyed())
	{
		if(!this->reconnect())
			return false;
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject();

	(*pBundle).newMessage(InterfacesInterface::onAccountLogin);
	(*pBundle) << pChannel->componentID();
	(*pBundle) << loginName << password;
	(*pBundle).appendBlob(datas);
	pInterfacesChannel->send(pBundle);
	return true;
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::onLoginAccountCB(Ouroboros::MemoryStream& s)
{
	std::string loginName, accountName, password, postdatas, getdatas;
	COMPONENT_ID cid;
	SERVER_ERROR_CODE success = SERVER_ERR_OP_FAILED;

	s >> cid >> loginName >> accountName >> password >> success;
	s.readBlob(postdatas);
	s.readBlob(getdatas);

	bool needCheckPassword = (success == SERVER_ERR_LOCAL_PROCESSING);

	if (success != SERVER_SUCCESS && success != SERVER_ERR_LOCAL_PROCESSING)
		accountName = "";
	else
		success = SERVER_SUCCESS;

	Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(LOGINAPP_TYPE, cid);
	if(cinfos == NULL || cinfos->pChannel == NULL)
	{
		ERROR_MSG("InterfacesHandler_Interfaces::onLoginAccountCB: loginapp not found!\n");
		return;
	}

	std::string dbInterfaceName = Dbmgr::getSingleton().selectAccountDBInterfaceName(accountName);

	thread::ThreadPool* pThreadPool = DBUtil::pThreadPool(dbInterfaceName);
	if (!pThreadPool)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Interfaces::onLoginAccountCB: not found dbInterface({})!\n",
			dbInterfaceName));

		return;
	}

	pThreadPool->addTask(new DBTaskAccountLogin(cinfos->pChannel->addr(),
		loginName, accountName, password, success, postdatas, getdatas, needCheckPassword));
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Interfaces::initialize()
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());
	if(pInterfacesChannel)
		return true;

	return reconnect();
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Interfaces::reconnect()
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());

	if(pInterfacesChannel)
	{
		if(!pInterfacesChannel->isDestroyed())
			Dbmgr::getSingleton().networkInterface().deregisterChannel(pInterfacesChannel);

		pInterfacesChannel->destroy();
		Network::Channel::reclaimPoolObject(pInterfacesChannel);
	}

	Network::Address addr = g_ouroSrvConfig.interfacesAddr();
	Network::EndPoint* pEndPoint = Network::EndPoint::createPoolObject();
	pEndPoint->addr(addr);

	pEndPoint->socket(SOCK_STREAM);
	if (!pEndPoint->good())
	{
		ERROR_MSG("InterfacesHandler_Interfaces::initialize: couldn't create a socket\n");
		return true;
	}

	pEndPoint->setnonblocking(true);
	pEndPoint->setnodelay(true);

	pInterfacesChannel = Network::Channel::createPoolObject();
	bool ret = pInterfacesChannel->initialize(Dbmgr::getSingleton().networkInterface(), pEndPoint, Network::Channel::INTERNAL);
	if(!ret)
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Interfaces::initialize: initialize({}) is failed!\n",
			pInterfacesChannel->c_str()));

		pInterfacesChannel->destroy();
		Network::Channel::reclaimPoolObject(pInterfacesChannel);
		return 0;
	}

	if(pInterfacesChannel->pEndPoint()->connect() == -1)
	{
		struct timeval tv = { 0, 2000000 }; // 1000ms
		fd_set frds, fwds;
		FD_ZERO( &frds );
		FD_ZERO( &fwds );
		FD_SET((int)(*pInterfacesChannel->pEndPoint()), &frds);
		FD_SET((int)(*pInterfacesChannel->pEndPoint()), &fwds);

		bool connected = false;
		int selgot = select((*pInterfacesChannel->pEndPoint())+1, &frds, &fwds, NULL, &tv);
		if(selgot > 0)
		{
			int error;
			socklen_t len = sizeof(error);
#if OURO_PLATFORM == PLATFORM_WIN32
			getsockopt(int(*pInterfacesChannel->pEndPoint()), SOL_SOCKET, SO_ERROR, (char*)&error, &len);
#else
			getsockopt(int(*pInterfacesChannel->pEndPoint()), SOL_SOCKET, SO_ERROR, &error, &len);
#endif
			if(0 == error)
				connected = true;
		}

		if(!connected)
		{
			ERROR_MSG(fmt::format("InterfacesHandler_Interfaces::reconnect(): couldn't connect to(interfaces server): {}! Check ouroboros[_defs].xml->interfaces->host and interfaces.*.log\n",
				pInterfacesChannel->pEndPoint()->addr().c_str()));

			pInterfacesChannel->destroy();
			Network::Channel::reclaimPoolObject(pInterfacesChannel);
			return false;
		}
	}

	// Do not check for timeout
	pInterfacesChannel->stopInactivityDetection();
	Dbmgr::getSingleton().networkInterface().registerChannel(pInterfacesChannel);
	return true;
}

//-------------------------------------------------------------------------------------
bool InterfacesHandler_Interfaces::process()
{
	return true;
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::charge(Network::Channel* pChannel, Ouroboros::MemoryStream& s)
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());
	OURO_ASSERT(pInterfacesChannel);

	if(pInterfacesChannel->isDestroyed())
	{
		if(!this->reconnect())
			return;
	}

	std::string chargeID;
	std::string datas;
	CALLBACK_ID cbid;
	DBID dbid;

	s >> chargeID;
	s >> dbid;
	s.readBlob(datas);
	s >> cbid;

	INFO_MSG(fmt::format("InterfacesHandler_Interfaces::charge: chargeID={0}, dbid={3}, cbid={1}, datas={2}!\n",
		chargeID, cbid, datas, dbid));

	Network::Bundle* pBundle = Network::Bundle::createPoolObject();

	(*pBundle).newMessage(InterfacesInterface::charge);
	(*pBundle) << pChannel->componentID();
	(*pBundle) << chargeID;
	(*pBundle) << dbid;
	(*pBundle).appendBlob(datas);
	(*pBundle) << cbid;
	pInterfacesChannel->send(pBundle);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::onChargeCB(Ouroboros::MemoryStream& s)
{
	std::string chargeID;
	std::string datas;
	CALLBACK_ID cbid;
	COMPONENT_ID cid;
	DBID dbid;
	SERVER_ERROR_CODE retcode;

	s >> cid;
	s >> chargeID;
	s >> dbid;
	s.readBlob(datas);
	s >> cbid;
	s >> retcode;

	INFO_MSG(fmt::format("InterfacesHandler_Interfaces::onChargeCB: chargeID={0}, dbid={3}, cbid={1}, cid={4}, datas={2}!\n",
		chargeID, cbid, datas, dbid, cid));

	Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(BASEAPP_TYPE, cid);
	if (cid == 0 || cinfos == NULL || cinfos->pChannel == NULL || cinfos->pChannel->isDestroyed())
	{
		ERROR_MSG(fmt::format("InterfacesHandler_Interfaces::onChargeCB: baseapp not found!, chargeID={}, cid={}.\n",
			chargeID, cid));

		// At this point should be randomly find a baseapp call onLoseChargeCB
		bool found = false;

		Components::COMPONENTS& components = Components::getSingleton().getComponents(BASEAPP_TYPE);
		for (Components::COMPONENTS::iterator iter = components.begin(); iter != components.end(); ++iter)
		{
			cinfos = &(*iter);
			if (cinfos == NULL || cinfos->pChannel == NULL || cinfos->pChannel->isDestroyed())
			{
				continue;
			}

			WARNING_MSG(fmt::format("InterfacesHandler_Interfaces::onChargeCB: , chargeID={}, not found cid={}, forward to component({}) processing!\n",
				chargeID, cid, cinfos->cid));

			found = true;
			break;
		}

		if (!found)
			return;
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject();

	(*pBundle).newMessage(BaseappInterface::onChargeCB);
	(*pBundle) << chargeID;
	(*pBundle) << dbid;
	(*pBundle).appendBlob(datas);
	(*pBundle) << cbid;
	(*pBundle) << retcode;

	cinfos->pChannel->send(pBundle);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::eraseClientReq(Network::Channel* pChannel, std::string& logkey)
{
	Network::Channel* pInterfacesChannel = Dbmgr::getSingleton().networkInterface().findChannel(g_ouroSrvConfig.interfacesAddr());
	OURO_ASSERT(pInterfacesChannel);

	if(pInterfacesChannel->isDestroyed())
	{
		if(!this->reconnect())
			return;
	}

	Network::Bundle* pBundle = Network::Bundle::createPoolObject();

	(*pBundle).newMessage(InterfacesInterface::eraseClientReq);
	(*pBundle) << logkey;
	pInterfacesChannel->send(pBundle);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::accountActivate(Network::Channel* pChannel, std::string& scode)
{
	// This function does not support third-party systems, so it is performed as a local account system
	InterfacesHandler_Dbmgr::accountActivate(pChannel, scode);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::accountReqResetPassword(Network::Channel* pChannel, std::string& accountName)
{
	// This function does not support third-party systems, so it is performed as a local account system
	InterfacesHandler_Dbmgr::accountReqResetPassword(pChannel, accountName);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::accountResetPassword(Network::Channel* pChannel, std::string& accountName, std::string& newpassword, std::string& scode)
{
	// This function does not support third-party systems, so it is performed as a local account system
	InterfacesHandler_Dbmgr::accountResetPassword(pChannel, accountName, newpassword, scode);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::accountReqBindMail(Network::Channel* pChannel, ENTITY_ID entityID, std::string& accountName,
												   std::string& password, std::string& email)
{
	// This function does not support third-party systems, so it is performed as a local account system
	InterfacesHandler_Dbmgr::accountReqBindMail(pChannel, entityID, accountName, password, email);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::accountBindMail(Network::Channel* pChannel, std::string& username, std::string& scode)
{
	// This function does not support third-party systems, so it is performed as a local account system
	InterfacesHandler_Dbmgr::accountBindMail(pChannel, username, scode);
}

//-------------------------------------------------------------------------------------
void InterfacesHandler_Interfaces::accountNewPassword(Network::Channel* pChannel, ENTITY_ID entityID, std::string& accountName,
												   std::string& password, std::string& newpassword)
{
	// This function does not support third-party systems, so it is performed as a local account system
	InterfacesHandler_Dbmgr::accountNewPassword(pChannel, entityID, accountName, password, newpassword);
}

//-------------------------------------------------------------------------------------

}