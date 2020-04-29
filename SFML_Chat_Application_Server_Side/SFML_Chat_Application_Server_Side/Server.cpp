#include "Server.h"
#include<iostream>
Server::~Server()
{
	Stop();
}

void Server::BindTimeoutHandler(void(*l_handler)(const ClientID &))
{
	m_timeoutHandler = std::bind(l_handler, std::placeholders::_1);
}

bool Server::Send(const ClientID & l_id, sf::Packet & l_packet)
{
	sf::Lock lock(m_mutex);
	std::unordered_map<ClientID, ClientInfo>::iterator itr = m_clients.find(l_id);
	if(itr == m_clients.end())  return false;
	if (m_outgoing.send(l_packet, itr->second.m_clientIP, itr->second.m_clientPORT) 
			!= sf::Socket::Done)
	{
		std::cout << "Error sending a packet...\n";
		return false;
	}
	m_totalSent += l_packet.getDataSize();
	return true;
}

bool Server::Send(const sf::IpAddress & l_ip, const PortNumber & l_port, sf::Packet & l_packet)
{
	sf::Lock lock(m_mutex);
	if(m_outgoing.send(l_packet,l_ip,l_port) != sf::Socket::Done) return false;
	m_totalSent += l_packet.getDataSize();
	return true;
}

void Server::Broadcast(sf::Packet & l_packet, const ClientID & l_ignore)
{
	sf::Lock lock(m_mutex);
	for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
	{
		if (itr.first != l_ignore)
		{
			if (m_outgoing.send(l_packet, itr.second.m_clientIP, itr.second.m_clientPORT)
					!= sf::Socket::Done)
			{
				std::cout << "Error broadcasting a packet to client: "
					<< itr.first << "\n";
				continue;
			}
			m_totalSent += l_packet.getDataSize();
		}
	}
}

void Server::Listen()
{
	sf::IpAddress ip;
	PortNumber port;
	sf::Packet packet;
	while (m_running)
	{
		packet.clear();
		sf::Socket::Status status = m_incoming.receive(packet, ip, port);
		//Handling error
		if (status != sf::Socket::Done)
		{
			if (m_running)
			{
				std::cout << "Error receiving a packet from: "
					<< ip << ":" << port << ". Code: " << status << "\n";
				continue;
			}
			else
			{
				std::cout << "Socket unbound. \n";
				break;
			}
		}

		m_totalReceived += packet.getDataSize();

		//Validate data
		PacketID p_id;
		if (!(packet >> p_id)) continue; //Non-conventional packet
		PacketType id = (PacketType)p_id;
		if (id < PacketType::Disconnect || id >= PacketType::OutOfBound) continue; //Invalid Packet Type
		
		//Synchronize time of server side and client side
		if (id == PacketType::Heartbeat)
		{
			sf::Lock lock(m_mutex);
			for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
			{
				if (itr.second.m_clientIP != ip || itr.second.m_clientPORT != port) continue;
				if (!itr.second.m_heartbeatWaiting)
				{
					std::cout << "Invalid heartbeat packet received!"<<std::endl;
					break;
				}
				itr.second.m_latency = m_serverTime.asMilliseconds() - itr.second.m_heartbeatSent.asMilliseconds();
				itr.second.m_heartbeatSent = m_serverTime;
				itr.second.m_heartbeatWaiting = false;
				itr.second.m_heartbeatRetry = 0;
				std::cout << "Heartbeat received from " << itr.second.m_clientIP.toString() << ":" 
					<< std::to_string(itr.second.m_clientPORT)<<std::endl;
				break;
			}
		}
		else if (m_packetHandler)
		{
			m_packetHandler(ip, port, p_id, packet, this);
		}
	}
}

void Server::Update(const sf::Time & l_time)
{
	m_serverTime += l_time;
	//Handling time variable go out of boundary
	if (m_serverTime.asMilliseconds() < 0)
	{
		m_serverTime -= sf::milliseconds(sf::Int32(Network::HighestTimestamp));
		sf::Lock lock(m_mutex);
		for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
		{
			itr.second.m_lastHeartbeat = sf::milliseconds(
				std::abs(itr.second.m_lastHeartbeat.asMilliseconds() - sf::Int32(Network::HighestTimestamp)));
		}
	}

	//Check client's connection through heartbeat
	sf::Lock lock(m_mutex);
	for (std::unordered_map<ClientID, ClientInfo>::iterator itr = m_clients.begin();
		itr != m_clients.end();)
	{
		//Check Timeout
		sf::Int32 elapsed = m_serverTime.asMilliseconds() - itr->second.m_lastHeartbeat.asMilliseconds();
		if (elapsed >= HEARTBEAT_INTERVAL)
		{
			if (elapsed >= (sf::Int32)Network::ClientTimeout || itr->second.m_heartbeatRetry > HEARTBEAT_RETRIES)
			{
				//Remove Client
				std::cout << "Client " << itr->first << " has timed out.\n";
				if (m_timeoutHandler) m_timeoutHandler(itr->first);
				itr = m_clients.erase(itr);
				continue;
			}

			// Send heartbeat to client
			if (!itr->second.m_heartbeatWaiting ||
				(elapsed >= HEARTBEAT_INTERVAL * (itr->second.m_heartbeatRetry + 1)))
			{
				if (itr->second.m_heartbeatRetry >= 3)
				{
					std::cout << "Re-try(" << itr->second.m_heartbeatRetry 
						<< ") heartbeat for client " << itr->first << std::endl;
				}
				sf::Packet heartbeat;
				StampPacket(PacketType::Heartbeat, heartbeat);
				heartbeat << m_serverTime.asMilliseconds();
				Send(itr->first, heartbeat);
				if (itr->second.m_heartbeatRetry == 0)
				{
					itr->second.m_heartbeatSent = m_serverTime;
				}
				itr->second.m_heartbeatWaiting = true;
				itr->second.m_heartbeatRetry++;

				m_totalSent += heartbeat.getDataSize();
			}
		}
		itr++;
	}
}

void Server::Setup()	
{
	m_lastID = 0;
	m_running = false;
	m_totalSent = m_totalReceived = 0;
}

Server::Server(void(*l_handler)(sf::IpAddress &, const PortNumber &, const PacketID &, sf::Packet &, Server *)) :
	m_listeningThread(&Server::Listen,this)
{
	m_packetHandler = std::bind(l_handler,
		std::placeholders::_1, std::placeholders::_2,
		std::placeholders::_3, std::placeholders::_4,
		std::placeholders::_5);
}

void Server::DisconnectAll()
{
	if (!m_running) return;
	sf::Packet p;
	StampPacket(PacketType::Disconnect, p);
	Broadcast(p);
	sf::Lock lock(m_mutex);
	m_clients.clear();
}

bool Server::Start()
{
	if(m_running) return false;
	if (m_incoming.bind(unsigned short(Network::ServerPort)) != sf::Socket::Done) return false;
	m_outgoing.bind(sf::Socket::AnyPort);
	Setup();
	std::cout << "Incoming port: " << m_incoming.getLocalPort() << "\n";
	std::cout << "Incoming IP: " << sf::IpAddress::getLocalAddress() << "\n";
	m_listeningThread.launch();
	m_running = true;
	return true;
}

bool Server::Stop()
{
	if(!m_running) return false;
	DisconnectAll();
	m_running = false;
	m_incoming.unbind(); //Stop Listening thread
}

bool Server::IsRunning()
{
	return m_running;
}

unsigned int Server::GetClientCount()
{
	return m_lastID;
}

std::string Server::GetClientList()
{
	std::string list ="";
	for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
	{
		list += itr.second.m_clientIP.toString() + ":" + std::to_string(itr.second.m_clientPORT) + "\n";
	}
	return list;
}

sf::Mutex & Server::GetMutex()
{
	// TODO: insert return statement here
	return  m_mutex;
}

ClientID Server::AddClient(const sf::IpAddress & l_ip, const PortNumber & l_port)
{
	sf::Lock lock(m_mutex);
	for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
	{
		if (itr.second.m_clientIP == l_ip && itr.second.m_clientPORT == l_port) return ClientID(Network::NullID);
		
	}
	ClientID id = m_lastID;
	ClientInfo info(l_ip, l_port, m_serverTime);
	m_clients.emplace(id, info);
	m_lastID++;
	return id;
}

ClientID Server::GetClientID(const sf::IpAddress & l_ip, const PortNumber & l_port)
{
	sf::Lock lock(m_mutex);
	for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
	{
		if (itr.second.m_clientIP == l_ip && itr.second.m_clientPORT == l_port) return itr.first;
	}
	return ClientID(Network::NullID);
}

bool Server::HasClient(const ClientID & l_id)
{
	sf::Lock lock(m_mutex);
	std::unordered_map<ClientID, ClientInfo>::iterator itr = m_clients.find(l_id);
	return (itr == m_clients.end() ? false : true);
}

bool Server::HasClient(const sf::IpAddress & l_ip, const PortNumber & l_port)
{
	sf::Lock lock(m_mutex);
	for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
	{
		if (itr.second.m_clientIP == l_ip && itr.second.m_clientPORT == l_port) return true;
	}
	return false;
}

bool Server::GetClientInfo(const ClientID & l_id, ClientInfo & l_info)
{
	sf::Lock lock(m_mutex);
	std::unordered_map<ClientID, ClientInfo>::iterator itr = m_clients.find(l_id);
	if (itr != m_clients.end())
	{
		l_info = itr->second;
		return true;
	}
	return false;
}

bool Server::RemoveClient(const ClientID & l_id)
{
	sf::Lock lock(m_mutex);
	std::unordered_map<ClientID, ClientInfo>::iterator itr = m_clients.find(l_id);
	if (itr == m_clients.end()) return false;
	sf::Packet p;
	StampPacket(PacketType::Disconnect, p);
	Send(l_id, p);
	m_clients.erase(itr);
	return true;
}

bool Server::RemoveClient(const sf::IpAddress & l_ip, const PortNumber & l_port)
{
	sf::Lock lock(m_mutex);
	for (std::unordered_map<ClientID, ClientInfo>::value_type & itr : m_clients)
	{
		if (itr.second.m_clientIP == l_ip && itr.second.m_clientPORT == l_port)
		{
			sf::Packet p;
			StampPacket(PacketType::Disconnect, p);
			Send(itr.first, p);
			m_clients.erase(itr.first);
			return true;
		}

	}
	return false;
}
