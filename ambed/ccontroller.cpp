//
//  ccontroller.cpp
//  ambed
//
//  Created by Jean-Luc Deltombe (LX3JL) on 15/04/2017.
//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.
//  Copyright © 2020 Thomas A. Early, N7TAE
//
// ----------------------------------------------------------------------------
//    This file is part of ambed.
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

#include "main.h"
#include "ctimepoint.h"
#include "cvocodecs.h"
#include "ccontroller.h"


////////////////////////////////////////////////////////////////////////////////////////
// constructor

CController::CController()
{
	keep_running = true;
	m_uiLastStreamId = 0;
}

////////////////////////////////////////////////////////////////////////////////////////
// destructor

CController::~CController()
{
	// close socket
	m_Socket.Close();

	// close all streams
	m_Mutex.lock();
	{
		for ( auto it=m_Streams.begin(); it!=m_Streams.end(); it++ )
		{
			delete *it;
		}
		m_Streams.clear();

	}

	m_Mutex.unlock();
	keep_running = false;
	Close();
}

////////////////////////////////////////////////////////////////////////////////////////
// initialization

bool CController::Init(void)
{
	// reset stop flag
	keep_running = true;

	// create our socket
	CIp ip(strchr(m_addr, ':') ? AF_INET6 : AF_INET, TRANSCODER_PORT, m_addr);
	if (! ip.IsSet())
	{
		std::cerr << "IP initialization failed for " << m_addr << std::endl;
		return false;
	}
	if (! m_Socket.Open(ip))
	{
		std::cout << "Error opening socket on port UDP" << TRANSCODER_PORT << " on ip " << ip.GetAddress() << std::endl;
		return false;
	}
	// start  thread;
	m_Future = std::async(std::launch::async, &CController::Task, this);

	return true;
}

void CController::Close(void)
{
	keep_running = false;
	if (m_Future.valid() )
	{
		m_Future.get();
	}
}

////////////////////////////////////////////////////////////////////////////////////////
// task

void CController::Task(void)
{
	while (keep_running) {
		CBuffer     Buffer;
		CIp         Ip;
		CCallsign   Callsign;
		uint8       CodecIn;
		uint8       CodecOut;
		uint16      StreamId;
		CStream     *Stream;

		// anything coming in from codec client ?
		if ( m_Socket.Receive(Buffer, Ip, 20) )
		{
			// crack packet
			if ( IsValidOpenstreamPacket(Buffer, &Callsign, &CodecIn, &CodecOut) )
			{
				std::cout << "Stream Open from " << Callsign << std::endl;

				// try create the stream
				Stream = OpenStream(Callsign, Ip, CodecIn, CodecOut);

				// send back details
				if ( Stream != NULL )
				{
					EncodeStreamDescrPacket(&Buffer, *Stream);
				}
				else
				{
					EncodeNoStreamAvailablePacket(&Buffer);
				}
				m_Socket.Send(Buffer, Ip);
			}
			else if ( IsValidClosestreamPacket(Buffer, &StreamId) )
			{
				// close the stream
				CloseStream(StreamId);

				std::cout << "Stream " << (int)StreamId << " closed" << std::endl;
			}
			else if ( IsValidKeepAlivePacket(Buffer, &Callsign) )
			{
				//std::cout << "ping - " << Callsign << std::endl;
				// pong back
				EncodeKeepAlivePacket(&Buffer);
				m_Socket.Send(Buffer, Ip);
			}
		}
	}

	// any inactive streams?
	Lock();
	for ( auto it=m_Streams.begin(); it!=m_Streams.end(); )
	{
		if ( !(*it)->IsActive() )
		{
			std::cout << "Stream " << (*it)->GetId() << " activity timeout " << std::endl;
			(*it)->Close();
			delete *it;
			it = m_Streams.erase(it);
		}
		else
		{
			it++;
		}
	}
	Unlock();
}

////////////////////////////////////////////////////////////////////////////////////////
// streams management

CStream *CController::OpenStream(const CCallsign &Callsign, const CIp &Ip, uint8 CodecIn, uint8 CodecOut)
{
	CStream *stream = NULL;

	// create a new stream
	m_uiLastStreamId = (m_uiLastStreamId + 1);
	m_uiLastStreamId = (m_uiLastStreamId == NB_MAX_STREAMS+1) ? 1 : m_uiLastStreamId;
	stream = new CStream(m_uiLastStreamId, Callsign, Ip, CodecIn, CodecOut);
	if ( stream->Init(TRANSCODER_PORT+m_uiLastStreamId) )
	{
		std::cout << "Opened stream " << m_uiLastStreamId << std::endl;
		// and store it
		Lock();
		m_Streams.push_back(stream);
		Unlock();
	}
	else
	{
		delete stream;
		stream = NULL;
	}

	// done
	return stream;
}

void CController::CloseStream(uint16 StreamId)
{
	Lock();
	// look for the stream
	for ( auto it=m_Streams.begin(); it!=m_Streams.end(); it++ )
	{
		// compare object pointers
		if ( (*it)->GetId() ==  StreamId )
		{
			// close it
			(*it)->Close();
			// remove it
			//std::cout << "Stream " << m_Streams[i]->GetId() << " removed" << std::endl;
			delete *it;
			m_Streams.erase(it);
			break;
		}
	}
	Unlock();
}

////////////////////////////////////////////////////////////////////////////////////////
// packet decoding helpers

bool CController::IsValidKeepAlivePacket(const CBuffer &Buffer, CCallsign *Callsign)
{
	uint8 tag[] = { 'A','M','B','E','D','P','I','N','G' };

	bool valid = false;
	if ( (Buffer.size() == 17) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// get callsign here
		Callsign->SetCallsign(&(Buffer.data()[9]), 8);
		valid = Callsign->IsValid();
	}
	return valid;
}

bool CController::IsValidOpenstreamPacket(const CBuffer &Buffer, CCallsign *Callsign, uint8 *CodecIn, uint8 *CodecOut)
{
	uint8 tag[] = { 'A','M','B','E','D','O','S' };

	bool valid = false;
	if ( (Buffer.size() == 17) && (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// get callsign here
		Callsign->SetCallsign(&(Buffer.data()[7]), 8);
		*CodecIn = Buffer.data()[15];
		*CodecOut = Buffer.data()[16];

		// valid ?
		valid = Callsign->IsValid() && IsValidCodecIn(*CodecIn) && IsValidCodecOut(*CodecOut);
	}
	return valid;
}

bool CController::IsValidClosestreamPacket(const CBuffer &Buffer, uint16 *StreamId)
{
	uint8 tag[] = { 'A','M','B','E','D','C','S' };

	bool valid = false;
	if ( /*(Buffer.size() == 16) &&*/ (Buffer.Compare(tag, sizeof(tag)) == 0) )
	{
		// get stream id
		*StreamId = *(uint16 *)(&Buffer.data()[7]);
		valid = true;
	}
	return valid;
}

////////////////////////////////////////////////////////////////////////////////////////
// packet encoding helpers

void CController::EncodeKeepAlivePacket(CBuffer *Buffer)
{
	uint8 tag[] = { 'A','M','B','E','D','P','O','N','G' };

	Buffer->Set(tag, sizeof(tag));
}

void CController::EncodeStreamDescrPacket(CBuffer *Buffer, const CStream &Stream)
{
	uint8 tag[] = { 'A','M','B','E','D','S','T','D' };

	Buffer->Set(tag, sizeof(tag));
	// id
	Buffer->Append((uint16)Stream.GetId());
	// port
	Buffer->Append((uint16)Stream.GetPort());
	// codecin
	Buffer->Append((uint8)Stream.GetCodecIn());
	// codecout
	Buffer->Append((uint8)Stream.GetCodecOut());
}

void CController::EncodeNoStreamAvailablePacket(CBuffer *Buffer)
{
	uint8 tag[] = { 'A','M','B','E','D','B','U','S','Y' };

	Buffer->Set(tag, sizeof(tag));
}


////////////////////////////////////////////////////////////////////////////////////////
// codec helpers

bool CController::IsValidCodecIn(uint8 codec)
{
	return ((codec == CODEC_AMBEPLUS) || (codec == CODEC_AMBE2PLUS) );
}

bool CController::IsValidCodecOut(uint8 codec)
{
	return ((codec == CODEC_AMBEPLUS) || (codec == CODEC_AMBE2PLUS) );
}
