/*
 * Oreka -- A media capture and retrieval platform
 *
 * Copyright (C) 2005, orecx LLC
 *
 * http://www.orecx.com
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 * Please refer to http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "SocketStreamer.h"
#include "ace/Thread_Manager.h"
#include "LogManager.h"
#include "Utils.h"
#include "ace/SOCK_Connector.h"
#include "ace/OS_NS_unistd.h"


/* /1* #ifdef UTEST *1/ */

/* #undef FLOG_DEBUG */
/* #undef FLOG_INFO */
/* #undef FLOG_WARN */
/* #undef FLOG_ERROR */

/* #define FLOG_DEBUG(logger,fmt, ...) ; */
/* #define FLOG_INFO(logger,fmt, ...) ; */
/* #define FLOG_WARN(logger,fmt, ...) ; */
/* #define FLOG_ERROR(logger,fmt, ...) ; */

/* /1* #endif *1/ */

static LoggerPtr getLog() {
	static LoggerPtr s_log = Logger::getLogger("socketstreamer");
	return s_log;
}

void SocketStreamer::ThreadHandler(void *args)
{
	SetThreadName("orka:sockstream");

	CStdString logMsg;
	SocketStreamer* ssc = (SocketStreamer*) args; 

	CStdString params = ssc->m_logMsg;
	FLOG_INFO(getLog(), "Succesfully created thread (%s)", params);

	CStdString ipPort = params;
	bool connected = false;
	time_t lastLogTime = 0;
	int bytesRead = 0;
	unsigned long int bytesSoFar = 0;

	while(1) {
		if (!connected) {
			lastLogTime = 0;
			while (!ssc->Connect()) {
				if (time(NULL) - lastLogTime > 60 ) {
					FLOG_WARN(getLog(), "Couldn't connect to: %s error: %s", ipPort, CStdString(strerror(errno)));
					lastLogTime = time(NULL);
				}
				NANOSLEEP(2,0);
			}
			connected=true;
			lastLogTime = 0;
		}

		bytesRead = ssc->Recv();
		if(bytesRead <= 0)
		{
			CStdString errorString(bytesRead==0?"Remote host closed connection":strerror(errno));
			FLOG_WARN(getLog(), "Connection to: %s closed. error :%s ", ipPort, errorString);
			ssc->Close();
			connected = false;
			continue;
		}

		bytesSoFar += bytesRead;
		if (time(NULL) - lastLogTime > 60 ) {
			FLOG_INFO(getLog(),"Read %d from %s so far", FormatDataSize(bytesSoFar), ipPort);
			lastLogTime = time(NULL);
		}
		NANOSLEEP(0,1);
	}
}

bool SocketStreamer::Parse(CStdString target) 
{
	CStdString ip;
	ChopToken(ip,":",target);

	if (!ACE_OS::inet_aton((PCSTR)ip, &m_ip)) {
		m_logMsg.Format("Invalid host:%s", ip);
		return false;
	}
	m_logMsg += CStdString(" host:") + ip;

	m_port = strtol(target,NULL,0);
	if (m_port == 0) {
		m_logMsg.Format("Invalid port:%s", target);
		return false;
	}
	m_logMsg += CStdString(" port:") + target;

	return true;
}

bool SocketStreamer::Connect()
{
	char szIp[16];
	ACE_INET_Addr server;
	ACE_SOCK_Connector connector;

	memset(m_buf, 0, sizeof(m_buf));

	memset(&szIp, 0, sizeof(szIp));
	ACE_OS::inet_ntop(AF_INET, (void*)&m_ip, szIp, sizeof(szIp));

	server.set(m_port, szIp);
	if(connector.connect(m_peer, server) == -1) {
		return false;
	}
	return Handshake();
}

void SocketStreamer::Close() {
	m_peer.close();
}

size_t SocketStreamer::Recv() {
	CStdString logMsg;
	m_bytesRead = m_peer.recv(m_buf, sizeof(m_buf));

	if (m_bytesRead>0) {
		ProcessData();
		FLOG_INFO(getLog(),"DATA : %d",m_bytesRead);
	}
	return m_bytesRead;
}

void SocketStreamer::ProcessData() {
	// default do nothing
}

bool SocketStreamer::Handshake() {
	return true; // default no handshake
}

void SocketStreamer::Initialize(std::list<CStdString>& targetList, SocketStreamerFactory *factory)
{
	CStdString logMsg;
	SocketStreamerFactory ssf;

	if (factory == NULL) {
		factory = &ssf;
	}

	for (std::list<CStdString>::iterator it = targetList.begin(); it != targetList.end(); it++)
	{
		CStdString target=*it;

		CStdString protocol;
		ChopToken(protocol,"://",target);
		protocol.ToLower();

		if (factory->Accepts(protocol)) {

			SocketStreamer *ss = factory->Create();
			ss->m_logMsg.Format("protocol:%s",protocol);

			if (!ss->Parse(target) || !ss->Spawn()) {
				FLOG_ERROR(getLog(),"Target:%s - %s", *it, ss->m_logMsg);
				delete ss;
			}
		}
	}
}

bool SocketStreamer::Spawn() 
{
	if (!ACE_Thread_Manager::instance()->spawn(ACE_THR_FUNC(ThreadHandler), (void*)this)) {
		m_logMsg = "Failed to start thread";
		return false;
	}
	return true;
}

