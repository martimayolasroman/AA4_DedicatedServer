#pragma once

#include <SFML/Network.hpp>
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <SFML/System/Time.hpp>

// Asumo que PlayerState.h está en el mismo directorio o en include path
#include "PlayerState.h"

// Constantes de juego para el servidor
const float PLAYER_SERVER_SPEED = 250.0f; // Pixels por segundo, igual que en el cliente
const unsigned int SERVER_MAP_WIDTH = 1024; // Ancho del mapa del juego
const unsigned int SERVER_MAP_HEIGHT = 768; // <--- NUEVO: Altura del mapa
const float TILE_SIZE = 32.0f; // <--- NUEVO: Asumo el tamaño del tile para colisiones en el servidor

const float GRAVITY_SERVER = 1200.0f; // <--- NUEVO: Gravedad en el servidor (debe coincidir con el cliente)
const float JUMP_STRENGTH_SERVER = 650.0f; // <--- NUEVO: Fuerza de salto en el servidor (debe coincidir con el cliente)
const float GAME_ROOM_PLAYER_WIDTH = TILE_SIZE * 0.9f; // <--- NUEVO: Dimensiones del jugador en el servidor
const float GAME_ROOM_PLAYER_HEIGHT = TILE_SIZE * 1.4f; // <--- NUEVO
const int PLAYER_INITIAL_HEALTH = 5;
const int PLAYER_INITIAL_LIVES = 3;
const int PLAYER_INITIAL_POS_X = 100;
const int PLAYER_INITIAL_POS_Y = 100;
// Valores numéricos alineados con Utils.h/PacketType del cliente
enum GameRoomPacketType {
    GR_C_PLAYER_INPUT = 5,
    GR_S_GAME_STATE = 107,
    C_PLAYER_TAUNT=108,
    S_OPPONENT_TAUNT=109
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
        const std::string& mapFilePath); // <--- NUEVO PARÁMETRO

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

    sf::IpAddress player2_address;
    unsigned short player2_port;
    PlayerState player2_state;

    // Para la simulación del mapa en el servidor (ej. un simple suelo)
    std::vector<sf::RectangleShape> server_platforms; // <--- NUEVO: Para las colisiones del servidor

    // Mapa de plataformas del servidor
    std::vector<sf::RectangleShape> m_server_map_platforms; // <--- NUEVO
    std::vector<sf::RectangleShape> loadServerMap(const std::string& filename);

    const sf::Time gameTickInterval = sf::milliseconds(4); // ~60 ticks por segundo (1000ms / 16ms = 62.5 TPS)

    void updatePlayerState(PlayerState& playerstate, float deltaTime);
    void updateGameState(float deltaTime);
    void sendGameStateToClients();
};