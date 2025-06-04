#pragma once

#include <SFML/Network.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "PlayerState.h" // Asumo que PlayerState.h está en el mismo directorio o en include path

// Constantes de juego para el servidor
const float PLAYER_SERVER_SPEED = 250.0f; // Pixels por segundo, igual que en el cliente
// Podrías añadir más constantes aquí si las necesitas (GRAVITY_SERVER, JUMP_SERVER_STRENGTH, etc.)
// para la física del lado del servidor. Por ahora, solo movimiento horizontal.
const unsigned int SERVER_MAP_WIDTH = 1024; // Ancho del mapa del juego, para límites


// Valores numéricos alineados con Utils.h/PacketType del cliente
enum GameRoomPacketType {
    GR_C_PLAYER_INPUT = 5,
    GR_S_GAME_STATE = 107,
};

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
        sf::IpAddress p2Addr, unsigned short p2Port);

    void run();
    void stop();

    std::string getRoomId() const { return id; }
    bool isRunning() const { return running_flag.load(); } // Usar .load() para atomics

    void processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet);

private:
    std::string id;
    std::atomic<bool> running_flag;
    sf::UdpSocket& game_socket; // Socket UDP principal del servidor, pasado por referencia

    // Información y estado de los jugadores
    sf::IpAddress player1_address;
    unsigned short player1_port;
    PlayerState player1_state;

    sf::IpAddress player2_address;
    unsigned short player2_port;
    PlayerState player2_state;

    // Control del bucle de juego
    sf::Clock gameLogicClock;
    const sf::Time gameTickInterval = sf::milliseconds(16); // ~60 ticks por segundo

    // Funciones privadas de la lógica de la sala
    void updatePlayerState(PlayerState& playerstate, float deltaTime); // Nueva función para actualizar un jugador
    void updateGameState(float deltaTime); // Ahora toma deltaTime
    void sendGameStateToClients();
};