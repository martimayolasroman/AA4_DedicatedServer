#include "GameRoom.h"

#include <iostream>
#include <thread> // Para std::this_thread::sleep_for

// Definición de tipos de paquete para el Gameplay (ejemplo)
enum  GamePacketType  {
    C_PLAYER_INPUT = 0, // Cliente envía sus inputs
    S_GAME_STATE = 1, 
    S_PLAYER_SHOOT_EFFECT = 2,
    S_PLAYER_DIED = 3 
  
};

// Operadores para el enum (si no los tienes globales)
sf::Packet& operator<<(sf::Packet& packet, GamePacketType type) {
    return packet << static_cast<int>(type);
}
sf::Packet& operator>>(sf::Packet& packet, GamePacketType& type) {
    int temp;
    packet >> temp;
    type = static_cast<GamePacketType>(temp);
    return packet;
}


GameRoom::GameRoom(const std::string& roomId, sf::UdpSocket& serverSocket, sf::IpAddress p1Addr, unsigned short p1Port, sf::IpAddress p2Addr, unsigned short p2Port)
    : id(roomId),
    running_flag(false), // Se pondrá a true en run()
    game_socket(serverSocket),
    player1_address(p1Addr), player1_port(p1Port),
    player2_address(p2Addr), player2_port(p2Port)
{


    std::cout << "[GameRoom " << id << "] Creada para "
        << player1_address.toString() << ":" << player1_port << " y "
        << player2_address.toString() << ":" << player2_port << std::endl;
    // Inicializar estados de jugadores si es necesario
    player1_state.position = { 100.f, 100.f }; // Ejemplo
    player2_state.position = { 200.f, 100.f }; // Ejemplo



}

void GameRoom::run()
{
    running_flag = true;
    std::cout << "[GameRoom " << id << "] Iniciando bucle de juego." << std::endl;

    gameLogicClock.restart();

    while (running_flag) {
        // Procesar inputs (se haría si los inputs se encolan,
        // pero aquí los procesamos directamente en processUdpPacket)

        if (gameLogicClock.getElapsedTime() >= gameTickInterval) {
            updateGameState();
            sendGameStateToClients();
            gameLogicClock.restart();
        }

        // Pequeña pausa para no consumir 100% CPU si no hay nada que hacer
        // y para dar tiempo a otros threads (como el de red del servidor)
       // std::this_thread::sleep_for(sf::milliseconds(5));
    }
    std::cout << "[GameRoom " << id << "] Bucle de juego detenido." << std::endl;
}

void GameRoom::stop()
{
    running_flag = false;
}

void GameRoom::processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet)
{
    if (!running_flag) return; // No procesar si la sala no está activa

    GamePacketType packetType;
    if (!(packet >> packetType)) {
        std::cerr << "[GameRoom " << id << "] Error al leer GamePacketType." << std::endl;
        return;
    }

    if (packetType == GamePacketType::C_PLAYER_INPUT) {
        // Determinar qué jugador envió el input
        PlayerState* targetPlayerState = nullptr;
        if (remoteAddress == player1_address && remotePort == player1_port) {
            targetPlayerState = &player1_state;
        }
        else if (remoteAddress == player2_address && remotePort == player2_port) {
            targetPlayerState = &player2_state;
        }
        else {
            std::cerr << "[GameRoom " << id << "] Paquete de input de un desconocido: "
                << remoteAddress.toString() << ":" << remotePort << std::endl;
            return;
        }

        // Extraer los inputs del paquete
        // Ejemplo: el cliente envía su dirección de movimiento y si quiere disparar
        float moveDirInput;
        bool shootInput;
        if (packet >> moveDirInput >> shootInput) {
            targetPlayerState->moveDirection = moveDirInput;
            targetPlayerState->wantsToShoot = shootInput;
            // std::cout << "[GameRoom " << id << "] Input recibido de " << remoteAddress.toString()
            //           << " Move: " << moveDirInput << " Shoot: " << shootInput << std::endl;
        }
        else {
            std::cerr << "[GameRoom " << id << "] Error al leer datos de input del paquete." << std::endl;
        }
    }
    else {
        std::cerr << "[GameRoom " << id << "] Tipo de paquete UDP desconocido: " << static_cast<int>(packetType) << std::endl;
    }
}

void GameRoom::updateGameState()
{
    // Lógica de juego muy simple: mover jugadores basado en su input
    const float playerSpeed = 50.0f * gameTickInterval.asSeconds(); // Unidades por tick -> Unidades por segundo

    // Actualizar Jugador 1
    player1_state.position.x += player1_state.moveDirection * playerSpeed;
    if (player1_state.wantsToShoot) {
        std::cout << "[GameRoom " << id << "] Jugador 1 (" << player1_address.toString() << ") disparó!" << std::endl;
        player1_state.wantsToShoot = false; // Resetear el input de disparo
    }
    // Aquí iría la lógica de colisiones, daño, etc.

    // Actualizar Jugador 2
    player2_state.position.x += player2_state.moveDirection * playerSpeed;
    if (player2_state.wantsToShoot) {
        std::cout << "[GameRoom " << id << "] Jugador 2 (" << player2_address.toString() << ") disparó!" << std::endl;
        player2_state.wantsToShoot = false;
    }

    // Lógica de vidas, respawn, etc.
    // Por ejemplo, si un jugador pierde toda la salud:
    // if (player1_state.health <= 0) {
    //    player1_state.lives--;
    //    if (player1_state.lives > 0) {
    //        player1_state.health = 5; // Respawnea con salud completa
    //        player1_state.position = {100.f, 100.f}; // Posición de respawn
    //    } else {
    //        // Jugador 1 pierde la partida
    //        std::cout << "[GameRoom " << id << "] Jugador 1 ha perdido todas las vidas." << std::endl;
    //        // Aquí se podría notificar el fin de partida y luego llamar a stop()
    //    }
    // }


}

void GameRoom::sendGameStateToClients()
{
    sf::Packet gameStatePacket;
    gameStatePacket << GamePacketType::S_GAME_STATE;

    // Empaquetar estado del jugador 1
    gameStatePacket << player1_state.position.x << player1_state.position.y
        << player1_state.health << player1_state.lives;
    // Empaquetar estado del jugador 2
    gameStatePacket << player2_state.position.x << player2_state.position.y
        << player2_state.health << player2_state.lives;
    // ... y cualquier otro estado relevante (balas, etc.)

    // Enviar a ambos jugadores
    if (game_socket.send(gameStatePacket, player1_address, player1_port) != sf::Socket::Status::Done) {
        std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 1." << std::endl;
    }
    if (game_socket.send(gameStatePacket, player2_address, player2_port) != sf::Socket::Status::Done) {
        std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 2." << std::endl;
    }
    // std::cout << "[GameRoom " << id << "] Estado de juego enviado." << std::endl;


}
