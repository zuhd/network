/*	
 *
 */

#ifndef _COCO_SOCKET_H_
#define _COCO_SOCKET_H_


#ifdef WIN32
#include<winsock.h>
typedef int socklen_t;

#define ERR_EAGAIN	WSAEWOULDBLOCK
#define	ERR_EINTR	WSAEINTR
#define ERR_EINPROGRESS  WSAEWOULDBLOCK
#define fcntl ioctlsocket
#define close closesocket

#pragma comment(lib,"wsock32")
class CSocketPrepare
{
public:
	CSocketPrepare()
	{
		WSADATA wsaData;
		WORD version = MAKEWORD(2,0);
		::WSAStartup(version,&wsaData);
	}
	~CSocketPrepare()
	{
		WSACleanup();
	}
};
#else
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<signal.h>
#include <errno.h>
#define INVALID_SOCKET        -1
#define SOCKET_ERROR        -1

#define ERR_EAGAIN	EAGAIN
#define	ERR_EINTR	EINTR
#define ERR_EINPROGRESS  EINPROGRESS
#define GetLastError() (errno)
class CSocketPrepare
{
public:
	CSocketPrepare()
	{
		struct sigaction sa;
		sa.sa_handler = SIG_IGN;
		sa.sa_flags = 0;
		if (sigemptyset(&sa.sa_mask) == -1 ||
			sigaction(SIGPIPE, &sa, 0) == -1) {
				perror("failed to ignore SIGPIPE; sigaction");
				exit(EXIT_FAILURE);
		}
	}
};
#endif

class CNetPacketParser
{
	enum
	{
		KeySize = 16,
	};
public:
	CNetPacketParser(void);
	~CNetPacketParser(void);
	virtual int EncodePacket(const char* pInData, int nInLen, char* pOutData, int& nOutLen);
	virtual int DecodePacket(const char* pInData, int nInLen, char* pOutData, int& nOutLen);
private:
	unsigned char	m_szSymmKey[16];	// ∂‘≥∆√‹‘ø
	unsigned short	m_shSeed;
};
//----------------------------------------------------------
class IBuffer
{
	char* buffer;
	unsigned int length;
	unsigned int LEN;
public:
	IBuffer(char* p,unsigned int l)
		:buffer(p)
		,length(0)
		,LEN(l)
	{

	}
public:
	char* GetPtr()
	{
		return buffer;
	}

	int GetLength()
	{
		return length;
	}

	int GetFreeLength()
	{
		return LEN - length;
	}

	void Fill(unsigned int len)
	{
		length += len;
	}

	void Fill(const void* data,unsigned int len)
	{
		memcpy(buffer+length,data,len);
		length += len;
	}

	void Shrink(unsigned int len)
	{
		memmove(buffer,buffer+len,length - len);
		length -= len;
	}
};

template<int MAX_LEN>
class CBuffer:public IBuffer
{
	char buffer[MAX_LEN];
public:
	CBuffer():IBuffer(buffer,MAX_LEN)
	{

	}
};

//------------------------------------------------------------------------------------

class ISocketHandler
{
public:
	virtual void OnConnection() = 0;
	virtual void OnRecv(const char* msg,int len) = 0;
	virtual void OnDisConnection() = 0;
	virtual void OnDisConnect() = 0;
};

class CocoSocket
{
	friend class CocoSocketReactor;
	char m_szIp[256];
	unsigned short m_usPort;
	int m_fd;
	bool m_bConnected;
	CBuffer<10240> m_ReadBuffer;
	CBuffer<10240> m_WriteBuffer;
	ISocketHandler* m_pSessionHandler;
	bool m_bWritable;
	bool m_bReadable;
	float m_TimeOut;
	float m_CurrentTime;
	CNetPacketParser m_pPacketParser;
public:
	CocoSocket();

	void setHandler(ISocketHandler* p){ m_pSessionHandler = p;}
	int fd(){ return m_fd; }
	const char* ip()const { return m_szIp; }
	unsigned short	port()const { return m_usPort; }
	bool isConnected(){ return m_bConnected; }
	bool connect(const char* ip, unsigned short port, float timeout = -1);
	int forceClose();
	void shutdown();
	void write(const char* data, int len);
public:
	static bool dnsParse(const char* domain, char*ip);
private:
	bool create(int af, int type, int protocol = 0);
	void onErrorEvent();
	bool onReadEvent();
	bool onWriteEvent();
	void onConnectedEvent();
	void onConnectFailedEvent();
	void onMessageEvent(const char* msg, int len);
	void onDisconnectedEvent();
	int onDispatch(const char* data, int len);
};

class CocoSocketReactor
{
	friend class CocoSocket;
	std::list<CocoSocket*> m_ConnectorList;
private:
	CocoSocketReactor();
public:
	static CocoSocketReactor& getInstance();
public:
	void update(float dt);
	void handleEvents();
	void addSocket(CocoSocket* tor);
	void delSocket(CocoSocket* tor);
};

#endif