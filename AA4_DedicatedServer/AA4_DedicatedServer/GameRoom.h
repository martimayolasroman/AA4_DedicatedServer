#pragma once

#include <SFML/Network.hpp>
#include <SFML/Graphics.hpp> // Necesario para sf::RectangleShape y sf::FloatRect
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <SFML/System/Time.hpp>
#include <fstream>
#include <iostream>
#include <optional>

#include "PlayerState.h"

// Constantes de juego para el servidor (DEBEN COINCIDIR EXACTAMENTE CON EL CLIENTE)
const float PLAYER_SERVER_SPEED = 250.0f;
const unsigned int SERVER_MAP_WIDTH = 1024;
const unsigned int SERVER_MAP_HEIGHT = 768;
const float TILE_SIZE = 32.0f;

const float GRAVITY_SERVER = 1200.0f;
const float JUMP_STRENGTH_SERVER = 650.0f; // <--- Asegúrate que coincide con JUMP_STRENGTH del cliente
const float GAME_ROOM_PLAYER_WIDTH = TILE_SIZE * 0.9f;
const float GAME_ROOM_PLAYER_HEIGHT = TILE_SIZE * 1.4f;
const int PLAYER_INITIAL_HEALTH = 5;
const int PLAYER_INITIAL_LIVES = 3;
const float PLAYER_INITIAL_POS_X = 100.0f;
const float PLAYER_INITIAL_POS_Y = 500.0f; // Ajusta esto para que los jugadores aparezcan sobre el suelo

// --- CONSTANTES DE BALA EN EL SERVIDOR (DEBEN COINCIDIR CON EL CLIENTE) ---
const float BULLET_SERVER_SPEED = 500.0f;
const float BULLET_SERVER_RADIUS = 5.0f; // Coincide con BULLET_RADIUS del cliente
const float SHOOT_SERVER_COOLDOWN = 2.0f; // Coincide con SHOOT_COOLDOWN del cliente

// Estructura para las balas del servidor
struct ServerBullet {
    sf::Vector2f position;
    sf::Vector2f velocity;
    float radius;
    bool isActive;
    int ownerPlayerId; // 1 o 2, para saber quién disparó y aplicar daño

    ServerBullet(sf::Vector2f pos, sf::Vector2f vel, float r, int ownerId)
        : position(pos), velocity(vel), radius(r), isActive(true), ownerPlayerId(ownerId) {}
};


// Valores numéricos alineados con Utils.h/PacketType del cliente
enum GameRoomPacketType {
    GR_C_PLAYER_INPUT = 5,
    GR_S_GAME_STATE = 107,
};

// Operadores para facilitar el uso de GameRoomPacketType con sf::Packet
inline sf::Packet& operator<<(sf::Packet& packet, GameRoomPacketType type) {
    return packet << static_cast<int>(type);
}
inline sf::Packet& operator>>(sf::Packet& packet, GameRoomPacketType& type) {
    int temp;
    packet >> temp;
    type = static_cast<GameRoomPacketType>(temp);
    return packet;
}

class GameRoom {
public:
    GameRoom(const std::string& roomId,
        sf::UdpSocket& serverSocket,
        sf::IpAddress p1Addr, unsigned short p1Port,
        sf::IpAddress p2Addr, unsigned short p2Port,
        const std::string& mapFilePath);

    void run();
    void stop();

    std::string getRoomId() const { return id; }
    bool isRunning() const { return running_flag.load(); }

    void processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet);

private:
    std::string id;
    std::atomic<bool> running_flag;
    sf::UdpSocket& game_socket;

    sf::IpAddress player1_address;
    unsigned short player1_port;
    PlayerState player1_state;
    float player1_shoot_cooldown;

    sf::IpAddress player2_address;
    unsigned short player2_port;
    PlayerState player2_state;
    float player2_shoot_cooldown;

    std::vector<sf::RectangleShape> m_server_map_platforms;
    std::vector<ServerBullet> m_server_bullets;

    const sf::Time gameTickInterval = sf::milliseconds(16); // Aproximadamente 60 TPS

    std::vector<sf::RectangleShape> loadServerMap(const std::string& filename);

    void updatePlayerState(PlayerState& playerstate, float deltaTime, float& shootCooldown, int playerId);
    void updateBulletsServer(float deltaTime);
    void updateGameState(float deltaTime);
    void sendGameStateToClients();
};