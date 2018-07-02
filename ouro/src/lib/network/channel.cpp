// 2017-2018 Rotten Visions, LLC. https://www.rottenvisions.com


#include "channel.h"
#ifndef CODE_INLINE
#include "channel.inl"
#endif

#include "network/websocket_protocol.h"
#include "network/websocket_packet_filter.h"
#include "network/websocket_packet_reader.h"
#include "network/bundle.h"
#include "network/packet_reader.h"
#include "network/network_interface.h"
#include "network/tcp_packet_receiver.h"
#include "network/tcp_packet_sender.h"
#include "network/udp_packet_receiver.h"
#include "network/kcp_packet_sender.h"
#include "network/kcp_packet_receiver.h"
#include "network/kcp_packet_reader.h"
#include "network/tcp_packet.h"
#include "network/udp_packet.h"
#include "network/message_handler.h"
#include "network/network_stats.h"

namespace Ouroboros {
namespace Network
{

//-------------------------------------------------------------------------------------
static ObjectPool<Channel> _g_objPool("Channel");
ObjectPool<Channel>& Channel::ObjPool()
{
	return _g_objPool;
}

//-------------------------------------------------------------------------------------
Channel* Channel::createPoolObject()
{
	return _g_objPool.createObject();
}

//-------------------------------------------------------------------------------------
void Channel::reclaimPoolObject(Channel* obj)
{
	_g_objPool.reclaimObject(obj);
}

//-------------------------------------------------------------------------------------
void Channel::destroyObjPool()
{
	DEBUG_MSG(fmt::format("Channel::destroyObjPool(): size {}.\n",
		_g_objPool.size()));

	_g_objPool.destroy();
}

//-------------------------------------------------------------------------------------
size_t Channel::getPoolObjectBytes()
{
	size_t bytes = sizeof(pNetworkInterface_) + sizeof(traits_) + sizeof(protocoltype_) + sizeof(protocolSubtype_) +
		sizeof(id_) + sizeof(inactivityTimerHandle_) + sizeof(inactivityExceptionPeriod_) +
		sizeof(lastReceivedTime_) + (bufferedReceives_.size() * sizeof(Packet*)) + sizeof(pPacketReader_) + (bundles_.size() * sizeof(Bundle*)) +
		+ sizeof(flags_) + sizeof(numPacketsSent_) + sizeof(numPacketsReceived_) + sizeof(numBytesSent_) + sizeof(numBytesReceived_)
		+ sizeof(lastTickBytesReceived_) + sizeof(lastTickBytesSent_) + sizeof(pFilter_) + sizeof(pEndPoint_) + sizeof(pPacketReceiver_) + sizeof(pPacketSender_)
		+ sizeof(proxyID_) + strextra_.size() + sizeof(channelType_)
		+ sizeof(componentID_) + sizeof(pMsgHandlers_);

	return bytes;
}

//-------------------------------------------------------------------------------------
Channel::SmartPoolObjectPtr Channel::createSmartPoolObj()
{
	return SmartPoolObjectPtr(new SmartPoolObject<Channel>(ObjPool().createObject(), _g_objPool));
}

//-------------------------------------------------------------------------------------
void Channel::onReclaimObject()
{
	this->clearState();
}

//-------------------------------------------------------------------------------------
void Channel::onEabledPoolObject()
{

}

//-------------------------------------------------------------------------------------
Channel::Channel(NetworkInterface & networkInterface,
		const EndPoint * pEndPoint, Traits traits, ProtocolType pt, ProtocolSubType spt,
		PacketFilterPtr pFilter, ChannelID id):
	pNetworkInterface_(NULL),
	traits_(traits),
	protocoltype_(pt),
	protocolSubtype_(spt),
	id_(id),
	inactivityTimerHandle_(),
	inactivityExceptionPeriod_(0),
	lastReceivedTime_(0),
	bundles_(),
	pPacketReader_(0),
	numPacketsSent_(0),
	numPacketsReceived_(0),
	numBytesSent_(0),
	numBytesReceived_(0),
	lastTickBytesReceived_(0),
	lastTickBytesSent_(0),
	pFilter_(pFilter),
	pEndPoint_(NULL),
	pPacketReceiver_(NULL),
	pPacketSender_(NULL),
	proxyID_(0),
	strextra_(),
	channelType_(CHANNEL_NORMAL),
	componentID_(UNKNOWN_COMPONENT_TYPE),
	pMsgHandlers_(NULL),
	flags_(0),
	pKCP_(NULL)
{
	this->clearBundle();
	initialize(networkInterface, pEndPoint, traits, pt, spt, pFilter, id);
}

//-------------------------------------------------------------------------------------
Channel::Channel():
	pNetworkInterface_(NULL),
	traits_(EXTERNAL),
	protocoltype_(PROTOCOL_TCP),
	protocolSubtype_(SUB_PROTOCOL_DEFAULT),
	id_(0),
	inactivityTimerHandle_(),
	inactivityExceptionPeriod_(0),
	lastReceivedTime_(0),
	bundles_(),
	pPacketReader_(0),
	// Stats
	numPacketsSent_(0),
	numPacketsReceived_(0),
	numBytesSent_(0),
	numBytesReceived_(0),
	lastTickBytesReceived_(0),
	lastTickBytesSent_(0),
	pFilter_(NULL),
	pEndPoint_(NULL),
	pPacketReceiver_(NULL),
	pPacketSender_(NULL),
	proxyID_(0),
	strextra_(),
	channelType_(CHANNEL_NORMAL),
	componentID_(UNKNOWN_COMPONENT_TYPE),
	pMsgHandlers_(NULL),
	flags_(0),
	pKCP_(NULL)
{
	this->clearBundle();
}

//-------------------------------------------------------------------------------------
Channel::~Channel()
{
	// DEBUG_MSG(fmt::format("Channel::~Channel(): {}\n", this->c_str()));
	finalise();
}

//-------------------------------------------------------------------------------------
bool Channel::initialize(NetworkInterface & networkInterface,
		const EndPoint * pEndPoint,
		Traits traits,
		ProtocolType pt,
		ProtocolSubType spt,
		PacketFilterPtr pFilter,
		ChannelID id)
{
	id_ = id;
	protocoltype_ = pt;
	protocolSubtype_ = spt;
	traits_ = traits;
	pFilter_ = pFilter;
	pNetworkInterface_ = &networkInterface;
	this->pEndPoint(pEndPoint);

	OURO_ASSERT(pNetworkInterface_ != NULL);
	OURO_ASSERT(pEndPoint_ != NULL);

	if(protocoltype_ == PROTOCOL_TCP)
	{
		if(pPacketReceiver_)
		{
			if(pPacketReceiver_->type() == PacketReceiver::UDP_PACKET_RECEIVER)
			{
				SAFE_RELEASE(pPacketReceiver_);
				pPacketReceiver_ = new TCPPacketReceiver(*pEndPoint_, *pNetworkInterface_);
			}
		}
		else
		{
			pPacketReceiver_ = new TCPPacketReceiver(*pEndPoint_, *pNetworkInterface_);
		}

		OURO_ASSERT(pPacketReceiver_->type() == PacketReceiver::TCP_PACKET_RECEIVER);

		// UDP does not require registration descriptor
		pNetworkInterface_->dispatcher().registerReadFileDescriptor(*pEndPoint_, pPacketReceiver_);

		// Need to send data to register again
		// pPacketSender_ = new TCPPacketSender(*pEndPoint_, *pNetworkInterface_);
		// pNetworkInterface_->dispatcher().registerWriteFileDescriptor(*pEndPoint_, pPacketSender_);

		if (pPacketSender_ && pPacketSender_->type() != PacketSender::TCP_PACKET_SENDER)
		{
			KCPPacketSender::reclaimPoolObject((KCPPacketSender*)pPacketSender_);
			pPacketSender_ = NULL;
		}
	}
	else
	{
		if (protocolSubtype_ == SUB_PROTOCOL_KCP)
		{
			if (pPacketReceiver_)
			{
				if (pPacketReceiver_->type() == PacketReceiver::TCP_PACKET_RECEIVER)
				{
					SAFE_RELEASE(pPacketReceiver_);
					pPacketReceiver_ = new KCPPacketReceiver(*pEndPoint_, *pNetworkInterface_);
				}
			}
			else
			{
				pPacketReceiver_ = new KCPPacketReceiver(*pEndPoint_, *pNetworkInterface_);
			}

			if (!init_kcp())
			{
				OURO_ASSERT(false);
				return false;
			}
		}
		else
		{
			if (pPacketReceiver_)
			{
				if (pPacketReceiver_->type() == PacketReceiver::TCP_PACKET_RECEIVER)
				{
					SAFE_RELEASE(pPacketReceiver_);
					pPacketReceiver_ = new UDPPacketReceiver(*pEndPoint_, *pNetworkInterface_);
				}
			}
			else
			{
				pPacketReceiver_ = new UDPPacketReceiver(*pEndPoint_, *pNetworkInterface_);
			}
		}

		OURO_ASSERT(pPacketReceiver_->type() == PacketReceiver::UDP_PACKET_RECEIVER);

		if (pPacketSender_ && pPacketSender_->type() != PacketSender::UDP_PACKET_SENDER)
		{
			TCPPacketSender::reclaimPoolObject((TCPPacketSender*)pPacketSender_);
			pPacketSender_ = NULL;
		}
	}

	pPacketReceiver_->pEndPoint(pEndPoint_);

	if(pPacketSender_)
		pPacketSender_->pEndPoint(pEndPoint_);

	startInactivityDetection((traits_ == INTERNAL) ? g_channelInternalTimeout :
													g_channelExternalTimeout,
							(traits_ == INTERNAL) ? g_channelInternalTimeout  / 2.f:
													g_channelExternalTimeout / 2.f);

	return true;
}

//-------------------------------------------------------------------------------------
bool Channel::finalise()
{
	this->clearState();

	SAFE_RELEASE(pPacketReceiver_);
	SAFE_RELEASE(pPacketReader_);
	SAFE_RELEASE(pPacketSender_);

	Network::EndPoint::reclaimPoolObject(pEndPoint_);
	pEndPoint_ = NULL;

	return true;
}

//-------------------------------------------------------------------------------------
IUINT32 createNewKCPID(IUINT32 arrayIndex)
{
	IUINT32 sessionID = 0;

	IUINT32 index = arrayIndex;
	index <<= 16;

	sessionID |= index;

	IUINT32 rnd = 0;

	srand(getSystemTime());
	rnd = (IUINT32)(rand() << 16);

	sessionID |= rnd;
	return sessionID;
}

bool Channel::init_kcp()
{
	static IUINT32 convID = 1;

	// To prevent overflow, theoretically normal use will not run out
	OURO_ASSERT(convID != 0);

	if(id_ == 0)
		id_ = convID++;

	pKCP_ = ikcp_create((IUINT32)id_, (void*)this);
	pKCP_->output = &Channel::kcp_output;

	// Configuration window size: average delay 200ms, send a packet every 20ms,
	// Considering packet loss and retransmission, set the maximum send/receive window to 128.
	int sndwnd = this->isExternal() ? Network::g_rudp_extWritePacketsQueueSize : Network::g_rudp_intWritePacketsQueueSize;
	int rcvwnd = this->isExternal() ? Network::g_rudp_extReadPacketsQueueSize : Network::g_rudp_intReadPacketsQueueSize;

	// nodelay-Several regular accelerations will start after enabling
	// Interval is the internal processing clock. The default setting is 10ms
	// Resend is a fast retransmission indicator, set to 2
	// Nc is whether to disable conventional flow control, this is forbidden
	int nodelay = Network::g_rudp_nodelay ? 1 : 0;
	int interval = Network::g_rudp_tickInterval;
	int resend = Network::g_rudp_missAcksResend;
	int disableNC = (!Network::g_rudp_congestionControl) ? 1 : 0;
	int minrto = Network::g_rudp_minRTO;

	if (this->isExternal() && Network::g_rudp_mtu > 0 && Network::g_rudp_mtu < (PACKET_MAX_SIZE_UDP * 4))
	{
		ikcp_setmtu(pKCP_, Network::g_rudp_mtu);
	}
	else
	{
		uint32 mtu = PACKET_MAX_SIZE_UDP - 72;
		if(pKCP_->mtu != mtu)
			ikcp_setmtu(pKCP_, mtu);
	}

	ikcp_wndsize(pKCP_, sndwnd, rcvwnd);
	ikcp_nodelay(pKCP_, nodelay, interval, resend, disableNC);
	pKCP_->rx_minrto = minrto;

	pKCP_->writelog = &Channel::kcp_writeLog;

	/*
	pKCP_->logmask |= (IKCP_LOG_OUTPUT | IKCP_LOG_INPUT | IKCP_LOG_SEND | IKCP_LOG_RECV | IKCP_LOG_IN_DATA | IKCP_LOG_IN_ACK |
		IKCP_LOG_IN_PROBE | IKCP_LOG_IN_WINS | IKCP_LOG_OUT_DATA | IKCP_LOG_OUT_ACK | IKCP_LOG_OUT_PROBE | IKCP_LOG_OUT_WINS);
	*/

	pNetworkInterface_->dispatcher().addTask(this);
	return true;
}

//-------------------------------------------------------------------------------------
bool Channel::fina_kcp()
{
	if (!pKCP_)
		return true;

	pNetworkInterface_->dispatcher().cancelTask(this);

	ikcp_release(pKCP_);
	pKCP_ = NULL;
	return true;
}

//-------------------------------------------------------------------------------------
void Channel::kcp_writeLog(const char *log, struct IKCPCB *kcp, void *user)
{
	Channel* pChannel = (Channel*)user;
	DEBUG_MSG(fmt::format("Channel::kcp_writeLog: {}, addr={}\n", log, pChannel->c_str()));
}

//-------------------------------------------------------------------------------------
int Channel::kcp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
	Channel* pChannel = (Channel*)user;

	if (pChannel->isCondemn())
		return -1;

	return ((KCPPacketSender*)pChannel->pPacketSender())->kcp_output(buf, len, kcp, pChannel);
}

//-------------------------------------------------------------------------------------
Channel * Channel::get(NetworkInterface & networkInterface,
			const Address& addr)
{
	return networkInterface.findChannel(addr);
}

//-------------------------------------------------------------------------------------
Channel * get(NetworkInterface & networkInterface,
		const EndPoint* pSocket)
{
	return networkInterface.findChannel(pSocket->addr());
}

//-------------------------------------------------------------------------------------
void Channel::startInactivityDetection( float period, float checkPeriod )
{
	stopInactivityDetection();

	// If the cycle is negative then do not check
	if(period > 0.001f)
	{
		checkPeriod = std::max(1.f, checkPeriod);
		inactivityExceptionPeriod_ = uint64( period * stampsPerSecond() ) - uint64( 0.05f * stampsPerSecond() );
		lastReceivedTime_ = timestamp();

		inactivityTimerHandle_ =
			this->dispatcher().addTimer( int( checkPeriod * 1000000 ),
										this, (void *)TIMEOUT_INACTIVITY_CHECK );
	}
}

//-------------------------------------------------------------------------------------
void Channel::stopInactivityDetection()
{
	inactivityTimerHandle_.cancel();
}

//-------------------------------------------------------------------------------------
void Channel::pEndPoint(const EndPoint* pEndPoint)
{
	if (pEndPoint_ != pEndPoint)
	{
		Network::EndPoint::reclaimPoolObject(pEndPoint_);
		pEndPoint_ = const_cast<EndPoint*>(pEndPoint);
	}

	lastReceivedTime_ = timestamp();
}

//-------------------------------------------------------------------------------------
void Channel::destroy()
{
	if(isDestroyed())
	{
		CRITICAL_MSG("is channel has Destroyed!\n");
		return;
	}

	clearState();
	flags_ |= FLAG_DESTROYED;
}

//-------------------------------------------------------------------------------------
void Channel::clearState( bool warnOnDiscard /*=false*/ )
{
	// Clear the unprocessed acceptance package cache
	if (bufferedReceives_.size() > 0)
	{
		BufferedReceives::iterator iter = bufferedReceives_.begin();
		int hasDiscard = 0;

		for(; iter != bufferedReceives_.end(); ++iter)
		{
			Packet* pPacket = (*iter);
			if(pPacket->length() > 0)
				hasDiscard++;

			RECLAIM_PACKET(pPacket->isTCPPacket(), pPacket);
		}

		if (hasDiscard > 0 && warnOnDiscard)
		{
			WARNING_MSG(fmt::format("Channel::clearState( {} ): "
				"Discarding {} buffered packet(s)\n",
				this->c_str(), hasDiscard));
		}

		bufferedReceives_.clear();
	}

	clearBundle();

	lastReceivedTime_ = timestamp();

	numPacketsSent_ = 0;
	numPacketsReceived_ = 0;
	numBytesSent_ = 0;
	numBytesReceived_ = 0;
	lastTickBytesReceived_ = 0;
	lastTickBytesSent_ = 0;
	proxyID_ = 0;
	strextra_ = "";
	channelType_ = CHANNEL_NORMAL;

	if(pEndPoint_ && protocoltype_ == PROTOCOL_TCP && !this->isDestroyed())
	{
		this->stopSend();

		if(pNetworkInterface_)
		{
			if(!this->isDestroyed())
				pNetworkInterface_->dispatcher().deregisterReadFileDescriptor(*pEndPoint_);
		}
	}

	// Only empty state here, do not release
	//SAFE_RELEASE(pPacketReader_);
	//SAFE_RELEASE(pPacketSender_);

	if (!fina_kcp())
	{
		OURO_ASSERT(false);
	}

	flags_ = 0;
	pFilter_ = NULL;

	stopInactivityDetection();

	// Since pEndPoint is usually given by an external source, it must be released and re-assigned when the channel is reactivated.
	if(pEndPoint_)
	{
		pEndPoint_->close();
		this->pEndPoint(NULL);
	}
}

//-------------------------------------------------------------------------------------
Channel::Bundles & Channel::bundles()
{
	return bundles_;
}

//-------------------------------------------------------------------------------------
const Channel::Bundles & Channel::bundles() const
{
	return bundles_;
}

//-------------------------------------------------------------------------------------
int32 Channel::bundlesLength()
{
	int32 len = 0;
	Bundles::iterator iter = bundles_.begin();
	for(; iter != bundles_.end(); ++iter)
	{
		len += (*iter)->packetsLength();
	}

	return len;
}

//-------------------------------------------------------------------------------------
void Channel::delayedSend()
{
	this->networkInterface().delayedSend(*this);
}

//-------------------------------------------------------------------------------------
const char * Channel::c_str() const
{
	static char dodgyString[ MAX_BUF ] = {"None"};
	char tdodgyString[ MAX_BUF ] = {0};

	if(pEndPoint_ && !pEndPoint_->addr().isNone())
		pEndPoint_->addr().writeToString(tdodgyString, MAX_BUF);

	ouro_snprintf(dodgyString, MAX_BUF, "%s/%d/%d/%d", tdodgyString, id_,
		this->isCondemn(), this->isDestroyed());

	return dodgyString;
}

//-------------------------------------------------------------------------------------
void Channel::clearBundle()
{
	Bundles::iterator iter = bundles_.begin();
	for(; iter != bundles_.end(); ++iter)
	{
		Bundle::reclaimPoolObject((*iter));
	}

	bundles_.clear();
}

//-------------------------------------------------------------------------------------
void Channel::handleTimeout(TimerHandle, void * arg)
{
	switch (reinterpret_cast<uintptr>(arg))
	{
		case TIMEOUT_INACTIVITY_CHECK:
		{
			if (timestamp() - lastReceivedTime_ >= inactivityExceptionPeriod_)
			{
				this->networkInterface().onChannelTimeOut(this);
			}
			break;
		}
		default:
			break;
	}
}

//-------------------------------------------------------------------------------------
void Channel::sendto(bool reliable, Bundle* pBundle)
{
	OURO_ASSERT(protocoltype_ == PROTOCOL_UDP);

	if (isDestroyed())
	{
		ERROR_MSG(fmt::format("Channel::sendto({}): channel has destroyed.\n",
			this->c_str()));

		this->clearBundle();

		if (pBundle)
			Network::Bundle::reclaimPoolObject(pBundle);

		return;
	}

	if (isCondemn())
	{
		//WARNING_MSG(fmt::format("Channel::sendto: error, reason={}, from {}.\n", reasonToString(REASON_CHANNEL_CONDEMN),
		//	c_str()));

		this->clearBundle();

		if (pBundle)
			Network::Bundle::reclaimPoolObject(pBundle);

		return;
	}

	if (pBundle)
	{
		pBundle->pChannel(this);
		pBundle->finiMessage(true);
		bundles_.push_back(pBundle);
	}

	uint32 bundleSize = (uint32)bundles_.size();
	if (bundleSize == 0)
		return;

	if (pPacketSender_ == NULL)
	{
		pPacketSender_ = KCPPacketSender::createPoolObject();
		pPacketSender_->pEndPoint(pEndPoint_);
		pPacketSender_->pNetworkInterface(pNetworkInterface_);
	}
	else
	{
		if (pPacketSender_->type() != PacketSender::UDP_PACKET_SENDER)
		{
			TCPPacketSender::reclaimPoolObject((TCPPacketSender*)pPacketSender_);
			pPacketSender_ = KCPPacketSender::createPoolObject();
			pPacketSender_->pEndPoint(pEndPoint_);
			pPacketSender_->pNetworkInterface(pNetworkInterface_);
		}
	}

	pPacketSender_->processSend(this, reliable ? 1 : 0);
	sendCheck(bundleSize);
}

//-------------------------------------------------------------------------------------
void Channel::send(Bundle* pBundle)
{
	if (protocoltype_ == PROTOCOL_UDP)
	{
		sendto(true, pBundle);
		return;
	}

	if (isDestroyed())
	{
		ERROR_MSG(fmt::format("Channel::send({}): channel has destroyed.\n",
			this->c_str()));

		this->clearBundle();

		if (pBundle)
			Network::Bundle::reclaimPoolObject(pBundle);

		return;
	}

	if (isCondemn())
	{
		//WARNING_MSG(fmt::format("Channel::send: error, reason={}, from {}.\n", reasonToString(REASON_CHANNEL_CONDEMN),
		//	c_str()));

		this->clearBundle();

		if (pBundle)
			Network::Bundle::reclaimPoolObject(pBundle);

		return;
	}

	if (pBundle)
	{
		pBundle->pChannel(this);
		pBundle->finiMessage(true);
		bundles_.push_back(pBundle);
	}

	uint32 bundleSize = (uint32)bundles_.size();
	if (bundleSize == 0)
		return;

	if (!sending())
	{
		if (pPacketSender_ == NULL)
		{
			pPacketSender_ = TCPPacketSender::createPoolObject();
			pPacketSender_->pEndPoint(pEndPoint_);
			pPacketSender_->pNetworkInterface(pNetworkInterface_);
		}
		else
		{
			if (pPacketSender_->type() != PacketSender::TCP_PACKET_SENDER)
			{
				KCPPacketSender::reclaimPoolObject((KCPPacketSender*)pPacketSender_);
				pPacketSender_ = TCPPacketSender::createPoolObject();
				pPacketSender_->pEndPoint(pEndPoint_);
				pPacketSender_->pNetworkInterface(pNetworkInterface_);
			}
		}

		pPacketSender_->processSend(this, 0);

		// If it cannot be sent to the system buffer immediately, then hand it to the poller
		if (bundles_.size() > 0 && !isCondemn() && !isDestroyed())
		{
			flags_ |= FLAG_SENDING;
			pNetworkInterface_->dispatcher().registerWriteFileDescriptor(*pEndPoint_, pPacketSender_);
		}
	}

	sendCheck(bundleSize);
}

//-------------------------------------------------------------------------------------
void Channel::sendCheck(uint32 bundleSize)
{
	if(this->isExternal())
	{
		if (Network::g_sendWindowMessagesOverflowCritical > 0 && bundleSize > Network::g_sendWindowMessagesOverflowCritical)
		{
			WARNING_MSG(fmt::format("Channel::sendCheck[{:p}]: external channel({}), send-window bufferedMessages has overflowed({} > {}).\n",
				(void*)this, this->c_str(), bundleSize, Network::g_sendWindowMessagesOverflowCritical));

			if (Network::g_extSendWindowMessagesOverflow > 0 &&
				bundleSize >  Network::g_extSendWindowMessagesOverflow)
			{
				ERROR_MSG(fmt::format("Channel::sendCheck[{:p}]: external channel({}), send-window bufferedMessages has overflowed({} > {}), Try adjusting the ouroboros[_defs].xml->windowOverflow->send->messages.\n",
					(void*)this, this->c_str(), bundleSize, Network::g_extSendWindowMessagesOverflow));

				this->condemn();
			}
		}

		if (g_extSendWindowBytesOverflow > 0)
		{
			uint32 bundleBytes = bundlesLength();
			if(bundleBytes >= g_extSendWindowBytesOverflow)
			{
				ERROR_MSG(fmt::format("Channel::sendCheck[{:p}]: external channel({}), bufferedBytes has overflowed({} > {}), Try adjusting the ouroboros[_defs].xml->windowOverflow->send->bytes.\n",
					(void*)this, this->c_str(), bundleBytes, g_extSendWindowBytesOverflow));

				this->condemn();
			}
		}
	}
	else
	{
		if (Network::g_sendWindowMessagesOverflowCritical > 0 && bundleSize > Network::g_sendWindowMessagesOverflowCritical)
		{
			if (Network::g_intSendWindowMessagesOverflow > 0 &&
				bundleSize > Network::g_intSendWindowMessagesOverflow)
			{
				ERROR_MSG(fmt::format("Channel::sendCheck[{:p}]: internal channel({}), send-window bufferedMessages has overflowed({} > {}).\n",
					(void*)this, this->c_str(), bundleSize, Network::g_intSendWindowMessagesOverflow));

				this->condemn();
			}
			else
			{
				WARNING_MSG(fmt::format("Channel::sendCheck[{:p}]: internal channel({}), send-window bufferedMessages has overflowed({} > {}).\n",
					(void*)this, this->c_str(), bundleSize, Network::g_sendWindowMessagesOverflowCritical));
			}
		}

		if (g_intSendWindowBytesOverflow > 0)
		{
			uint32 bundleBytes = bundlesLength();
			if (bundleBytes >= g_intSendWindowBytesOverflow)
			{
				WARNING_MSG(fmt::format("Channel::sendCheck[{:p}]: internal channel({}), bufferedBytes has overflowed({} > {}).\n",
					(void*)this, this->c_str(), bundleBytes, g_intSendWindowBytesOverflow));
			}
		}
	}
}

//-------------------------------------------------------------------------------------
void Channel::stopSend()
{
	if(!sending())
		return;

	flags_ &= ~FLAG_SENDING;

	pNetworkInterface_->dispatcher().deregisterWriteFileDescriptor(*pEndPoint_);
}

//-------------------------------------------------------------------------------------
void Channel::onSendCompleted()
{
	OURO_ASSERT(bundles_.size() == 0 && sending());
	stopSend();
}

//-------------------------------------------------------------------------------------
void Channel::onPacketSent(int bytes, bool sentCompleted)
{
	if(sentCompleted)
	{
		++numPacketsSent_;
		++g_numPacketsSent;
	}

	if (bytes > 0)
	{
		numBytesSent_ += bytes;
		g_numBytesSent += bytes;
		lastTickBytesSent_ += bytes;
	}

	if(this->isExternal())
	{
		if(g_extSentWindowBytesOverflow > 0 &&
			lastTickBytesSent_ >= g_extSentWindowBytesOverflow)
		{
			ERROR_MSG(fmt::format("Channel::onPacketSent[{:p}]: external channel({}), sentBytes has overflowed({} > {}), Try adjusting the ouroboros[_defs].xml->windowOverflow->send->tickSentBytes.\n",
				(void*)this, this->c_str(), lastTickBytesSent_, g_extSentWindowBytesOverflow));

			this->condemn();
		}
	}
	else
	{
		if(g_intSentWindowBytesOverflow > 0 &&
			lastTickBytesSent_ >= g_intSentWindowBytesOverflow)
		{
			WARNING_MSG(fmt::format("Channel::onPacketSent[{:p}]: internal channel({}), sentBytes has overflowed({} > {}).\n",
				(void*)this, this->c_str(), lastTickBytesSent_, g_intSentWindowBytesOverflow));
		}
	}
}

//-------------------------------------------------------------------------------------
void Channel::onPacketReceived(int bytes)
{
	lastReceivedTime_ = timestamp();
	++numPacketsReceived_;
	++g_numPacketsReceived;

	if (bytes > 0)
	{
		numBytesReceived_ += bytes;
		lastTickBytesReceived_ += bytes;
		g_numBytesReceived += bytes;
	}

	if(this->isExternal())
	{
		if(g_extReceiveWindowBytesOverflow > 0 &&
			lastTickBytesReceived_ >= g_extReceiveWindowBytesOverflow)
		{
			ERROR_MSG(fmt::format("Channel::onPacketReceived[{:p}]: external channel({}), bufferedBytes has overflowed({} > {}), Try adjusting the ouroboros[_defs].xml->windowOverflow->receive.\n",
				(void*)this, this->c_str(), lastTickBytesReceived_, g_extReceiveWindowBytesOverflow));

			this->condemn();
		}
	}
	else
	{
		if(g_intReceiveWindowBytesOverflow > 0 &&
			lastTickBytesReceived_ >= g_intReceiveWindowBytesOverflow)
		{
			WARNING_MSG(fmt::format("Channel::onPacketReceived[{:p}]: internal channel({}), bufferedBytes has overflowed({} > {}).\n",
				(void*)this, this->c_str(), lastTickBytesReceived_, g_intReceiveWindowBytesOverflow));
		}
	}
}

//-------------------------------------------------------------------------------------
void Channel::addReceiveWindow(Packet* pPacket)
{
	bufferedReceives_.push_back(pPacket);
	uint32 size = (uint32)bufferedReceives_.size();

	if(Network::g_receiveWindowMessagesOverflowCritical > 0 && size > Network::g_receiveWindowMessagesOverflowCritical)
	{
		if(this->isExternal())
		{
			if(Network::g_extReceiveWindowMessagesOverflow > 0 &&
				size > Network::g_extReceiveWindowMessagesOverflow)
			{
				ERROR_MSG(fmt::format("Channel::addReceiveWindow[{:p}]: external channel({}), receive window has overflowed({} > {}), Try adjusting the ouroboros[_defs].xml->windowOverflow->receive->messages->external.\n",
					(void*)this, this->c_str(), size, Network::g_extReceiveWindowMessagesOverflow));

				this->condemn();
			}
			else
			{
				WARNING_MSG(fmt::format("Channel::addReceiveWindow[{:p}]: external channel({}), receive window has overflowed({} > {}).\n",
					(void*)this, this->c_str(), size, Network::g_receiveWindowMessagesOverflowCritical));
			}
		}
		else
		{
			if(Network::g_intReceiveWindowMessagesOverflow > 0 &&
				size > Network::g_intReceiveWindowMessagesOverflow)
			{
				WARNING_MSG(fmt::format("Channel::addReceiveWindow[{:p}]: internal channel({}), receive window has overflowed({} > {}).\n",
					(void*)this, this->c_str(), size, Network::g_intReceiveWindowMessagesOverflow));
			}
		}
	}
}

//-------------------------------------------------------------------------------------
void Channel::condemn()
{
	if(isCondemn())
		return;

	flags_ |= FLAG_CONDEMN;
	//WARNING_MSG(fmt::format("Channel::condemn[{:p}]: channel({}).\n", (void*)this, this->c_str()));
}

//-------------------------------------------------------------------------------------
void Channel::handshake()
{
	if(hasHandshake())
		return;

	if(bufferedReceives_.size() > 0)
	{
		BufferedReceives::iterator packetIter = bufferedReceives_.begin();
		Packet* pPacket = (*packetIter);

		// Unconditionally set to handshake after an interaction, whether successful
		flags_ |= FLAG_HANDSHAKE;

		if (protocoltype_ == PROTOCOL_TCP)
		{
			// Here is a decision whether it is websocket or other protocol handshake
			if (websocket::WebSocketProtocol::isWebSocketProtocol(pPacket))
			{
				channelType_ = CHANNEL_WEB;
				if (websocket::WebSocketProtocol::handshake(this, pPacket))
				{
					if (pPacket->length() == 0)
					{
						bufferedReceives_.erase(packetIter);
					}

					if (!pPacketReader_ || pPacketReader_->type() != PacketReader::PACKET_READER_TYPE_WEBSOCKET)
					{
						SAFE_RELEASE(pPacketReader_);
						pPacketReader_ = new WebSocketPacketReader(this);
					}

					pFilter_ = new WebSocketPacketFilter(this);
					DEBUG_MSG(fmt::format("Channel::handshake: websocket({}) successfully!\n", this->c_str()));
					return;
				}
				else
				{
					DEBUG_MSG(fmt::format("Channel::handshake: websocket({}) error!\n", this->c_str()));
				}
			}
		}
		else
		{
			if (protocolSubtype_ == SUB_PROTOCOL_KCP)
			{
				std::string hello;
				(*pPacket) >> hello;
				pPacket->clear(false);

				bufferedReceives_.erase(packetIter);

				if (hello != UDP_HELLO)
				{
					DEBUG_MSG(fmt::format("Channel::handshake: kcp({}) error!\n", this->c_str()));
					this->condemn();
				}
				else
				{
					UDPPacket* pHelloAckUDPPacket = UDPPacket::createPoolObject();
					(*pHelloAckUDPPacket) << Network::UDP_HELLO_ACK << OUROVersion::versionString() << (uint32)id();
					pEndPoint()->sendto(pHelloAckUDPPacket->data(), pHelloAckUDPPacket->length());
					UDPPacket::reclaimPoolObject(pHelloAckUDPPacket);

					if (!pPacketReader_ || pPacketReader_->type() != PacketReader::PACKET_READER_TYPE_KCP)
					{
						SAFE_RELEASE(pPacketReader_);
						pPacketReader_ = new KCPPacketReader(this);
					}

					DEBUG_MSG(fmt::format("Channel::handshake: kcp({}) successfully!\n", this->c_str()));
					return;
				}
			}
		}

		if(!pPacketReader_ || pPacketReader_->type() != PacketReader::PACKET_READER_TYPE_SOCKET)
		{
			SAFE_RELEASE(pPacketReader_);
			pPacketReader_ = new PacketReader(this);
		}

		pPacketReader_->reset();
	}
}

//-------------------------------------------------------------------------------------
bool Channel::process()
{
	ikcp_update(pKCP_, ouro_clock());
	return true;
}

//-------------------------------------------------------------------------------------
void Channel::processPackets(Ouroboros::Network::MessageHandlers* pMsgHandlers)
{
	lastTickBytesReceived_ = 0;
	lastTickBytesSent_ = 0;

	if(pMsgHandlers_ != NULL)
	{
		pMsgHandlers = pMsgHandlers_;
	}

	if (this->isDestroyed())
	{
		ERROR_MSG(fmt::format("Channel::processPackets({}): channel[{:p}] is destroyed.\n",
			this->c_str(), (void*)this));

		return;
	}

	if(this->isCondemn())
	{
		ERROR_MSG(fmt::format("Channel::processPackets({}): channel[{:p}] is condemn.\n",
			this->c_str(), (void*)this));

		//this->destroy();
		return;
	}

	if(!hasHandshake())
	{
		handshake();
	}

	BufferedReceives::iterator packetIter = bufferedReceives_.begin();

	try
	{
		for(; packetIter != bufferedReceives_.end(); ++packetIter)
		{
			Packet* pPacket = (*packetIter);
			pPacketReader_->processMessages(pMsgHandlers, pPacket);
			RECLAIM_PACKET(pPacket->isTCPPacket(), pPacket);
		}
	}
	catch(MemoryStreamException &)
	{
		Network::MessageHandler* pMsgHandler = pMsgHandlers->find(pPacketReader_->currMsgID());
		WARNING_MSG(fmt::format("Channel::processPackets({}): packet invalid. currMsg=({}, id={}, len={}), currMsgLen={}\n",
			this->c_str()
			, (pMsgHandler == NULL ? "unknown" : pMsgHandler->name)
			, pPacketReader_->currMsgID()
			, (pMsgHandler == NULL ? -1 : pMsgHandler->msgLen)
			, pPacketReader_->currMsgLen()));

		pPacketReader_->currMsgID(0);
		pPacketReader_->currMsgLen(0);
		condemn();

		for (; packetIter != bufferedReceives_.end(); ++packetIter)
		{
			Packet* pPacket = (*packetIter);
			if (pPacket->isEnabledPoolObject())
			{
				RECLAIM_PACKET(pPacket->isTCPPacket(), pPacket);
			}
		}
	}

	bufferedReceives_.clear();
}

//-------------------------------------------------------------------------------------
bool Channel::waitSend()
{
	return pEndPoint()->waitSend();
}

//-------------------------------------------------------------------------------------
EventDispatcher & Channel::dispatcher()
{
	return pNetworkInterface_->dispatcher();
}

//-------------------------------------------------------------------------------------
Bundle* Channel::createSendBundle()
{
	if(bundles_.size() > 0)
	{
		Bundle* pBundle = bundles_.back();
		Bundle::Packets& packets = pBundle->packets();

		// Both pBundle and packages[0] must be objects that have not been reclaimed by the object pool
		// It must be an unencrypted packet. If it is already encrypted, it should not be used again. Otherwise, it is easy for external users to add unencrypted data to it.
		if (pBundle->packetHaveSpace() &&
			!packets[0]->encrypted())
		{
			// Remove from queue first
			bundles_.pop_back();
			pBundle->pChannel(this);
			pBundle->pCurrMsgHandler(NULL);
			pBundle->currMsgPacketCount(0);
			pBundle->currMsgLength(0);
			pBundle->currMsgLengthPos(0);
			if (!pBundle->pCurrPacket())
			{
				Packet* pPacket = pBundle->packets().back();
				pBundle->packets().pop_back();
				pBundle->pCurrPacket(pPacket);
			}

			return pBundle;
		}
	}

	Bundle* pBundle = Bundle::createPoolObject();
	pBundle->pChannel(this);
	return pBundle;
}

//-------------------------------------------------------------------------------------

}
}