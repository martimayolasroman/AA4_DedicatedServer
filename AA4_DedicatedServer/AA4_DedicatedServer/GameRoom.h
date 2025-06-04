#pragma once

#include <SFML/Network.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <mutex> // Aunque no se usa directamente aquí, es bueno tenerlo si se añaden más accesos concurrentes
#include <SFML/System/Time.hpp> // Para sf::Time, sf::Clock

#include "PlayerState.h" // Asumo que PlayerState.h está en el mismo directorio o en include path

// Constantes de juego para el servidor
const float PLAYER_SERVER_SPEED = 250.0f; // Pixels por segundo, igual que en el cliente
const unsigned int SERVER_MAP_WIDTH = 1024; // Ancho del mapa del juego, para límites
// GAME_ROOM_PLAYER_WIDTH se define en GameRoom.cpp

// Valores numéricos alineados con Utils.h/PacketType del cliente
// Asegúrate de que estos valores coincidan con el enum PacketType del Cliente
enum GameRoomPacketType {
    GR_C_PLAYER_INPUT = 5,  // Coincide con C_PLAYER_INPUT = 5 en Client.cpp
    GR_S_GAME_STATE = 107,  // Coincide con S_GAME_STATE = 107 en Client.cpp
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
        sf::UdpSocket& serverSocket, // Socket UDP principal del servidor (compartido)
        sf::IpAddress p1Addr, unsigned short p1Port,
        sf::IpAddress p2Addr, unsigned short p2Port);

    void run();  // Bucle principal de la sala de juego (se ejecutará en un hilo)
    void stop(); // Señal para detener el bucle run()

    std::string getRoomId() const { return id; }
    bool isRunning() const { return running_flag.load(); }

    // Procesa un paquete UDP recibido para esta sala
    void processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet);

private:
    std::string id;
    std::atomic<bool> running_flag; // Para controlar el bucle run() de forma segura entre hilos
    sf::UdpSocket& game_socket;    // Referencia al socket UDP principal del DedicatedServer

    // Información y estado de los jugadores
    sf::IpAddress player1_address;
    unsigned short player1_port;
    PlayerState player1_state; // Contiene posición, vida, inputs, etc.

    sf::IpAddress player2_address;
    unsigned short player2_port;
    PlayerState player2_state;

    // Control del bucle de juego con timestep fijo
    // gameLogicClock ya no es necesaria si usamos el patrón de acumulador en run()
    const sf::Time gameTickInterval = sf::milliseconds(16); // ~60 ticks por segundo (1000ms / 16ms = 62.5 TPS)
    // Puedes ajustarlo a sf::milliseconds(33) para ~30 TPS si prefieres

// Funciones privadas de la lógica de la sala
    void updatePlayerState(PlayerState& playerstate, float deltaTime);
    void updateGameState(float deltaTime); // deltaTime será gameTickInterval.asSeconds()
    void sendGameStateToClients();
};