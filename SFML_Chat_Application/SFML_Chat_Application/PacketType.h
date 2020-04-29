#pragma once
#include<SFML/Network.hpp>

using PacketID = sf::Int8;
enum class PacketType {
	Disconnect = -1,Connect, Heartbeat, Snapshot,
	Player_Update,Message,Hurt,OutOfBound
};

inline void StampPacket(const PacketType & l_type, sf::Packet & l_packet) {
	l_packet << PacketID(l_type);
}