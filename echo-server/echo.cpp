#include <iostream>
#include <string>
#include <exception>
#include <WinSock2.h>
#include <map>
#include <vector>
#include <memory>

#pragma comment(lib, "ws2_32.lib")

namespace echoservice
{
	using std::string;
	using std::vector;

	enum RESULT
	{
		kSUCCESS = 0,
		kFAIL = -1
	};

	namespace info
	{
		int nPort = 1234;
		int nBacklog = 5; // server waiting queue size.
		string serverIP("127.0.0.1");
	};

	namespace enumToString
	{
		string& WSAError(int code)
		{
			static std::map<int, string> map =
			{
				{WSAEINTR, "Interrupted function call."},
				{WSAEBADF, "File handle is not valid."},
				{WSAECONNRESET, "Connection reset by peer." },
				{WSAECONNREFUSED, "Connection refused."}
			};

			static string invalid("Invalid error code or unknown");

			if (map.find(code) == map.end()) return invalid;
			else return map[code];
		}
	};

	class NetworkExcpetion : public std::exception
	{
		string message;
	public:
		explicit NetworkExcpetion(const char* msg)
		{
			Message(string(msg));
		}

		explicit NetworkExcpetion(string&& msg)
		{
			Message(msg);
		}

		virtual const char* what() const throw() {
			return message.c_str();
		}
	private:
		void Message(string msg)
		{
			message = "[" + string(__FILE__) + "][" + std::to_string(__LINE__) + "]" 
				+ msg + " " + enumToString::WSAError(WSAGetLastError()) 
				+ "code :" + std::to_string(WSAGetLastError());
		}
	};

	__interface ISocket
	{
		virtual void InitSocket() = 0;
		virtual void CloseSocket(SOCKET*, bool) = 0;
	};

	__interface IServer
	{
		virtual void Listen() = 0;
		virtual bool Run() = 0;
	};

	__interface IClient
	{
		virtual void Connect(string& ip, int port);
	};

	class Socket : public ISocket
	{
	private:
		Socket(const Socket&);
		const Socket& operator=(const Socket&);

	protected:
		void CloseSocket(SOCKET* sc, bool force = false) override;

	public:
		Socket();
		virtual ~Socket();
		void InitSocket() override;

	protected:
		SOCKET m_socket;
	};

	Socket::Socket() : m_socket(INVALID_SOCKET) {}
	Socket::~Socket()
	{
		if (m_socket != INVALID_SOCKET)
			CloseSocket(&m_socket);

		WSACleanup();
	}

	void Socket::InitSocket()
	{
		WSADATA wsaData;
		int nStartUpRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (nStartUpRet != 0)
			throw NetworkExcpetion("Can't initalize WSA");

		auto sc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sc == INVALID_SOCKET) 
			throw NetworkExcpetion("INVALIDE_SOCKET");

		m_socket = sc;
	}

	void Socket::CloseSocket(SOCKET* sc, bool force)
	{
		if (sc == nullptr || *sc == INVALID_SOCKET) return;

		struct linger stLinger = { 0, 0 }; // SO_DONTLINGER로 설정
		// bIsForce가 true이면 SO_LINGER, timeout = 0으로 설정하여
		// 강제 종료 시킨다. 주의 테이터 손실이 있을 수 있다.
		if (force == true)
			stLinger.l_onoff = 1;

		// 소켓의 데이터 송수신을 모두 중단 시킨다.
		shutdown(*sc, SD_BOTH);

		// 소켓 옵션을 설정한다.
		setsockopt(*sc, SOL_SOCKET, SO_LINGER, 
			reinterpret_cast<char*>(&stLinger), sizeof(stLinger));

		// 소켓 연결을 종료 시킨다.
		closesocket(*sc);

		*sc = INVALID_SOCKET;
	}

	class Server : public Socket, public IServer
	{
	public:
		virtual ~Server() {}
		bool Run() override;
		void Listen() override;
	};

	bool Server::Run() 
	{
		static int nAddrLen = sizeof(sockaddr_in);

		sockaddr_in sockaddrClient;
		SOCKET sc = accept(m_socket, reinterpret_cast<sockaddr*>(&sockaddrClient), &nAddrLen);

		if (sc == INVALID_SOCKET)
			throw NetworkExcpetion("Accept error");

		auto destructor = [&](SOCKET* sc) { CloseSocket(sc, false); };
		std::unique_ptr<SOCKET, decltype(destructor)> _(&sc, destructor);

		while (true)
		{
			vector<char> buffer(1024, 0);
			auto nRecvRet = recv(sc, buffer.data(), buffer.size(), 0);
			if (nRecvRet == 0)
				return false;

			if (nRecvRet == SOCKET_ERROR)
				throw NetworkExcpetion("Recvied data - socket error");

			vector<char> recived(buffer.data(), buffer.data() + nRecvRet);

			std::cout << "RECIVED FROM CLINET: " << string(recived.begin(), recived.end()) << std::endl;
			int nSendLen = send(sc, recived.data(), buffer.size(), 0);
			if (nSendLen == SOCKET_ERROR)
				throw NetworkExcpetion("Send data - socket error");
		}
	}

	void Server::Listen()
	{
		sockaddr_in sockaddrServer;
		sockaddrServer.sin_family = AF_INET;
		sockaddrServer.sin_addr.s_addr = htonl(INADDR_ANY);
		sockaddrServer.sin_port = htons(info::nPort);

		int nBindRet = bind(m_socket, reinterpret_cast<sockaddr*>(&sockaddrServer), sizeof(sockaddr));
		if (nBindRet != 0)
			throw NetworkExcpetion("BIND_ERROR WITH ERROR_CODE " + std::to_string(nBindRet));

		int nListenRet = listen(m_socket, info::nBacklog);
		if (nListenRet != 0)
			throw NetworkExcpetion("Listen ERROR WITH ERROR_CODE " + std::to_string(nListenRet));
	}

	class Client : public Socket, public IClient
	{
	public:
		Client() {}
		virtual ~Client() {}
		void Connect(string& ip, int port) override;
	};

	void Client::Connect(string& ip, int port)
	{
		SOCKADDR_IN stSeverAddr;

		stSeverAddr.sin_family = AF_INET;
		stSeverAddr.sin_port = htons(port);
		stSeverAddr.sin_addr.s_addr = inet_addr(ip.c_str());

		int nConnectRet = connect(m_socket, reinterpret_cast<sockaddr*>(&stSeverAddr),
			sizeof(sockaddr));
		
		if (nConnectRet == SOCKET_ERROR)
			throw NetworkExcpetion("Connet error");

		while (true)
		{
			std::cout << ">> ";
			string str;
			str.reserve(1024);
			getline(std::cin, str);

			if (str.compare("QUIT()") == 0)
				break;

			vector<char> buffer(1024, 0);
			auto sentLength = min(buffer.size(), str.size());
			copy_n(str.begin(), sentLength, buffer.begin());

			int nSendRet = send(m_socket, buffer.data(), sentLength, 0);

			if (nSendRet == SOCKET_ERROR)
				throw NetworkExcpetion("Send error");

			int nRecvRet = recv(m_socket, buffer.data(), buffer.size(), 0);
			if (nRecvRet == 0)
				return;

			if (nRecvRet == SOCKET_ERROR)
				throw NetworkExcpetion("Recv error");

			if (nRecvRet > 0)
				std::cout << "RECEVIED FROM CLIENT: " << string(buffer.data(), buffer.data() + nSendRet) << std::endl;
		}
	}
};

int main(int argc, char** argv)
{
	using std::string;
	
	bool isServer = false;

	if (argc == 2 && (string(argv[1]).compare("/server") ||
				      string(argv[1]).compare("/client")))
	{
		isServer = string(argv[1]).compare("/server") == 0;
	}
	else if (argc == 1)
	{
		string input;
		getline(std::cin, input);
		if (input.compare("server") == 0)
			isServer = true;
		else if (input.compare("client") == 0)
			isServer = false;
	}
	else
	{
		std::cout << "Invalide option parameter" << std::endl; 
		return echoservice::kFAIL;
	}

	if (isServer)
	{
		echoservice::Server server;
		try {
			server.InitSocket();
			server.Listen();

			bool bRelunch = true;
			do
			{
				try 
				{
					bRelunch = server.Run();
				}
				catch (echoservice::NetworkExcpetion& e)
				{
					std::cout << "EXCEPTION : " << e.what() << std::endl;
					std::cout << "RETRY CONNETCTION" << std::endl;
				}
			} while (bRelunch);
		}
		catch (std::exception& e) {
			std::cout << "EXCEPTION : " << e.what() << std::endl;
		}
	}
	else
	{
		echoservice::Client client;
		try {
			client.InitSocket();
			client.Connect(echoservice::info::serverIP, echoservice::info::nPort);
		}
		catch (echoservice::NetworkExcpetion& e)
		{
			std::cout << "EXCEPTION : " << e.what() << std::endl;
		}
		catch (std::exception& e) 
		{
			std::cout << "EXCEPTION : " << e.what() << std::endl;
		}
	}

	return echoservice::kSUCCESS;
}