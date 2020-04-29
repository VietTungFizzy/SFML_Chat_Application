#pragma once
#include<SFML/Graphics.hpp>
#include<functional>
#include"PacketType.h"
#include"NetworkDefinition.h"

#define CONNTECTION_TIMEOUT 10000 //Milliseconds
class Client;
using PacketHandler = std::function<void(const PacketID &, sf::Packet &, Client *)>;

class Client
{
private:
	std::string m_PlayerName;
	sf::UdpSocket m_socket;
	sf::IpAddress m_serverIP;
	PortNumber m_serverPort;
	PacketHandler m_packetHandler;
	bool m_connected;
	sf::Time m_serverTime;
	sf::Time m_lastHeartbeat;

	sf::Thread m_listenThread;
	sf::Mutex m_mutex;

public:
	Client();
	~Client();
	bool Connect();
	bool Disconnect();
	void Listen();
	bool Send(sf::Packet & l_packet);
	const sf::Time & GetTime() const;
	const sf::Time & GetLastHeartbeat() const;
	void SetTime(const sf::Time & l_time);
	void SetServerInformation(const sf::IpAddress & l_ip, const PortNumber & l_port);
	template<class T>
	void Setup(void(T::*l_handler)(const PacketID &, sf::Packet&, Client*), T * l_instance) 
	{
		m_packetHandler = std::bind(l_handler, l_instance,
			std::placeholders::_1, std::placeholders::_2,
			std::placeholders::_3);
	}

	void Setup(void(*l_handler)(const PacketID &, sf::Packet&, Client*));
	void UnregisterPacketHandler();
	void Update(const sf::Time & l_time); // Main function is keep track of passing time
	bool isConnected() const;
	void SetPlayerName(const std::string & l_name);
	sf::Mutex & GetMutex();
};

