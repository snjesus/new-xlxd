//
//  cprotocol.h
//  xlxd
//
//  Created by Jean-Luc Deltombe (LX3JL) on 01/11/2015.
//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.
//  Copyright © 2020 Thomas A. Early, N7TAE
//
// ----------------------------------------------------------------------------
//    This file is part of xlxd.
//
//    xlxd is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    xlxd is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
// ----------------------------------------------------------------------------

#ifndef cprotocol_h
#define cprotocol_h

#include "cudpsocket.h"
#include "cpacketstream.h"
#include "cdvheaderpacket.h"
#include "cdvframepacket.h"
#include "cdvlastframepacket.h"

////////////////////////////////////////////////////////////////////////////////////////

// DMR defines
// slot n'
#define DMR_SLOT1                       1
#define DMR_SLOT2                       2
// call type
#define DMR_GROUP_CALL                  0
#define DMR_PRIVATE_CALL                1
// frame type
#define DMR_FRAMETYPE_VOICE             0
#define DMR_FRAMETYPE_VOICESYNC         1
#define DMR_FRAMETYPE_DATA              2
#define DMR_FRAMETYPE_DATASYNC          3
// data type
#define DMR_DT_VOICE_PI_HEADER          0
#define DMR_DT_VOICE_LC_HEADER          1
#define DMR_DT_TERMINATOR_WITH_LC       2
#define DMR_DT_CSBK                     3
#define DMR_DT_DATA_HEADER              6
#define DMR_DT_RATE_12_DATA             7
#define DMR_DT_RATE_34_DATA             8
#define DMR_DT_IDLE                     9
#define DMR_DT_RATE_1_DATA              10
// CRC masks
#define DMR_VOICE_LC_HEADER_CRC_MASK    0x96
#define DMR_TERMINATOR_WITH_LC_CRC_MASK 0x99
#define DMR_PI_HEADER_CRC_MASK          0x69
#define DMR_DATA_HEADER_CRC_MASK        0xCC
#define DMR_CSBK_CRC_MASK               0xA5


////////////////////////////////////////////////////////////////////////////////////////
// class

class CProtocol
{
public:
	// constructor
	CProtocol();

	// destructor
	virtual ~CProtocol();

	// initialization
	virtual bool Initialize(const char *type, const int ptype, const uint16 port, const bool has_ipv4, const bool has_ipv6);
	virtual void Close(void);

	// queue
	CPacketQueue *GetQueue(void)        { m_Queue.Lock(); return &m_Queue; }
	void ReleaseQueue(void)             { m_Queue.Unlock(); }

	// get
	const CCallsign &GetReflectorCallsign(void)const { return m_ReflectorCallsign; }

	// task
	void Thread(void);
	virtual void Task(void) = 0;

protected:
	// packet encoding helpers
	virtual bool EncodeDvPacket(const CPacket &, CBuffer *) const;
	virtual bool EncodeDvHeaderPacket(const CDvHeaderPacket &, CBuffer *) const         { return false; }
	virtual bool EncodeDvFramePacket(const CDvFramePacket &, CBuffer *) const           { return false; }
	virtual bool EncodeDvLastFramePacket(const CDvLastFramePacket &, CBuffer *) const   { return false; }

	// stream helpers
	virtual void OnDvHeaderPacketIn(std::unique_ptr<CDvHeaderPacket> &, const CIp &) {}
	virtual void OnDvFramePacketIn(std::unique_ptr<CDvFramePacket> &, const CIp * = nullptr);
	virtual void OnDvLastFramePacketIn(std::unique_ptr<CDvLastFramePacket> &, const CIp * = nullptr);

	// stream handle helpers
	CPacketStream *GetStream(uint16, const CIp * = nullptr);
	void CheckStreamsTimeout(void);

	// queue helper
	virtual void HandleQueue(void) = 0;

	// keepalive helpers
	virtual void HandleKeepalives(void) = 0;

	// syntax helper
	bool IsNumber(char) const;
	bool IsLetter(char) const;
	bool IsSpace(char) const;

	// dmr DstId to Module helper
	virtual char DmrDstIdToModule(uint32) const;
	virtual uint32 ModuleToDmrDestId(char) const;

	bool Receive6(CBuffer &buf, CIp &Ip, int time_ms);
	bool Receive4(CBuffer &buf, CIp &Ip, int time_ms);
	bool ReceiveDS(CBuffer &buf, CIp &Ip, int time_ms);

	void Send(const CBuffer &buf, const CIp &Ip) const;
	void Send(const char    *buf, const CIp &Ip) const;
	void Send(const CBuffer &buf, const CIp &Ip, uint16 port) const;
	void Send(const char    *buf, const CIp &Ip, uint16 port) const;

	// socket
	CUdpSocket m_Socket4;
	CUdpSocket m_Socket6;

	// streams
	std::list<CPacketStream *> m_Streams;

	// queue
	CPacketQueue    m_Queue;

	// thread
	std::atomic<bool> keep_running;
	std::future<void> m_Future;

	// identity
	CCallsign       m_ReflectorCallsign;

	// debug
	CTimePoint      m_DebugTimer;
};

////////////////////////////////////////////////////////////////////////////////////////
#endif /* cprotocol_h */
