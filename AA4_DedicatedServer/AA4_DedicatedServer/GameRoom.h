#pragma once


#include <SFML/Network.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <mutex> // Para proteger el acceso a datos si es necesario
#include "PlayerState.h"



class GameRoom
{

public:

    GameRoom(const std::string& roomId,
        sf::UdpSocket& serverSocket, // Referencia al socket UDP principal
        sf::IpAddress p1Addr, unsigned short p1Port,
        sf::IpAddress p2Addr, unsigned short p2Port);

    void run(); // Bucle principal de la lógica de esta sala
    void stop(); // Para detener el bucle de la sala

    std::string getRoomId() const { return id; }
    bool isRunning() const { return running_flag; }

    // Método para que el DedicatedServer le pase los paquetes UDP que le corresponden
    void processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet);

private:

    std::string id;
    std::atomic<bool> running_flag;
    sf::UdpSocket& game_socket; // Referencia al socket UDP del servidor

    // Información de los jugadores de esta sala
    sf::IpAddress player1_address;
    unsigned short player1_port;
    PlayerState player1_state;

    sf::IpAddress player2_address;
    unsigned short player2_port;
    PlayerState player2_state;

    // Relojes para controlar la tasa de actualización y envío
    sf::Clock gameLogicClock;
    sf::Time gameTickInterval = sf::milliseconds(16); // Aproximadamente 60 ticks por segundo

    void updateGameState();
    void sendGameStateToClients();


};

