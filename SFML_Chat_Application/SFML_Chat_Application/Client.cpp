#include "Client.h"
#include<iostream>


Client::Client():m_listenThread(&Client::Listen,this)
{
}


Client::~Client()
{
	m_socket.unbind();
}

bool Client::Connect()
{
	//Establish connection and send data to server
	if (m_connected) { return false; }
	m_socket.bind(sf::Socket::AnyPort);
	sf::Packet p;
	StampPacket(PacketType::Connect, p);
	p << m_PlayerName;
	if (m_socket.send(p, m_serverIP, m_serverPort) != sf::Socket::Done)
	{
		m_socket.unbind();
		return false;
	}
	m_socket.setBlocking(false);
	p.clear();
	sf::IpAddress recvIP;
	PortNumber recvPORT;
	sf::Clock timer;
	timer.restart();
	
	//Listening response from server
	while (timer.getElapsedTime().asMilliseconds() < CONNTECTION_TIMEOUT)
	{
		sf::Socket::Status s = m_socket.receive(p, recvIP, recvPORT);
		if (s != sf::Socket::Done) { continue; }
		if (recvIP != m_serverIP) { continue; }
		PacketID id;
		if (!(p >> id)) { break; }
		if ((PacketType)id != PacketType::Connect) { continue; }
		m_packetHandler(id, p, this);
		m_connected = true;
		m_socket.setBlocking(true);
		m_lastHeartbeat = m_serverTime;
		m_listenThread.launch();
		std::cout << "Connected to server " << recvIP.toString() << ":" << std::to_string(recvPORT)<<std::endl;
		return true;
	}
	//Handling timeout
	std::cout << "Connection attempt failed! Server info: "
		<< m_serverIP << ":" << m_serverPort << std::endl;
	m_socket.unbind();
	m_socket.setBlocking(true);
	return false;
}

bool Client::Disconnect()
{
	//Check Client's current status
	if (m_connected) { return false; }

	//Send disconnect message to server
	sf::Packet p;
	StampPacket(PacketType::Disconnect, p);
	sf::Socket::Status s = m_socket.send(p, m_serverIP, m_serverPort);
	m_connected = false;

	//unbind socket
	m_socket.unbind(); //unbind to close listening thread
	if (s != sf::Socket::Done) { return false; }
	return true;
}

void Client::Listen()
{
	sf::IpAddress recvIP;
	sf::Packet packet;
	PortNumber recvPORT;
	//Waiting for data
	while (m_connected)
	{
		packet.clear();
		sf::Socket::Status status = m_socket.receive(packet, recvIP, recvPORT);
		//Handling error (maybe because connection)
		if (status != sf::Socket::Done)
		{
			if (m_connected)
			{
				std::cout << "Failed to receiving a packet from "
					<< recvIP << ":" << recvPORT
					<< " Status: " << status <<"\n";
				continue;
			}
			else
			{
				std::cout << "Socket Unbound.\n";
				break;
			}
		}

		//Validate data
		if (recvIP != m_serverIP) { continue; } // ignore packets not sent from the server
		PacketID p_id;
		if (!(packet >> p_id)) { continue; } //Non-conventional packet
		PacketType id = (PacketType)p_id;
		if (id < PacketType::Disconnect || id >= PacketType::OutOfBound) { continue; } //Invalid packet type

		//Avoid time of server side and client side going out of sync
		if (id == PacketType::Heartbeat) 
		{
			sf::Packet p;
			StampPacket(PacketType::Heartbeat, p);
			//std::cout << "Sending hearbeat to server " << m_serverIP.toString() << ":" << std::to_string(m_serverPort) << std::endl;
			if (m_socket.send(p, m_serverIP, m_serverPort) != sf::Socket::Done)
			{
				std::cout << "Failed to sending a heartbeat!\n";
			}
			sf::Int32 timestamp;
			packet >> timestamp;
			SetTime(sf::milliseconds(timestamp));
			m_lastHeartbeat = m_serverTime;
			//std::cout << "Success sending heartbeat with content: " << std::to_string(timestamp) << std::endl;
		}
		else if(m_packetHandler) //Handle other packet
		{
			m_packetHandler((PacketID)id, packet, this);
		}
	}	
	

}

bool Client::Send(sf::Packet & l_packet)
{
	if (!m_connected) { return false; }
	if (m_socket.send(l_packet, m_serverIP, m_serverPort) != sf::Socket::Done) return true;
	return false;
}

const sf::Time & Client::GetTime() const
{
	return m_serverTime;
}

const sf::Time & Client::GetLastHeartbeat() const
{
	return m_lastHeartbeat;
}

void Client::SetTime(const sf::Time & l_time)
{
	m_serverTime = l_time;
}

void Client::SetServerInformation(const sf::IpAddress & l_ip, const PortNumber & l_port)
{
	m_serverIP = l_ip;
	m_serverPort = l_port;
}

void Client::Setup(void(*l_handler)(const PacketID &, sf::Packet &, Client *))
{
	m_packetHandler = std::bind(l_handler,
		std::placeholders::_1, std::placeholders::_2,
		std::placeholders::_3);
}

void Client::UnregisterPacketHandler()
{
	m_packetHandler = nullptr;
}

void Client::Update(const sf::Time & l_time)
{
	if (!m_connected) return;
	m_serverTime += l_time;

	//Avoid negative server time
	if (m_serverTime.asMilliseconds() < 0) 
	{
		m_serverTime -= sf::milliseconds(sf::Int32(Network::HighestTimestamp));
		m_lastHeartbeat = m_serverTime;
		return;
	}

	//Check timeout
	if (m_serverTime.asMilliseconds() - m_lastHeartbeat.asMilliseconds() >= 
		sf::Int32(Network::ClientTimeout))
	{
		std::cout << "Server connection timed out!\n";
		Disconnect();
	}
}

bool Client::isConnected() const
{
	return m_connected;
}

void Client::SetPlayerName(const std::string & l_name)
{
	m_PlayerName = l_name;
}

sf::Mutex & Client::GetMutex()
{
	// TODO: insert return statement here
	return m_mutex;
}
