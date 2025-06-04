#include "GameRoom.h"
#include <iostream>
#include <thread> // Para std::this_thread::sleep_for

const float GAME_ROOM_PLAYER_WIDTH = 32.0f * 0.9f;
const int PLAYER_INITIAL_HEALTH = 5;
const int PLAYER_INITIAL_LIVES = 3;


GameRoom::GameRoom(const std::string& roomId, sf::UdpSocket& serverSocket, sf::IpAddress p1Addr, unsigned short p1Port, sf::IpAddress p2Addr, unsigned short p2Port)
    : id(roomId),
    running_flag(false), // Se establece a true en run()
    game_socket(serverSocket),
    player1_address(p1Addr), player1_port(p1Port),
    player2_address(p2Addr), player2_port(p2Port) {

  /*  std::cout << "[GameRoom " << id << "] Creada para P1:"
        << player1_address.toString() << ":" << player1_port << " y P2:"
        << player2_address.toString() << ":" << player2_port << std::endl;*/

    // Inicializar estados de jugadores (posiciones y stats)
    player1_state.position = { 100.f, static_cast<float>(SERVER_MAP_WIDTH) / 2.f }; // Ajustar Y si es necesario
    player1_state.health = PLAYER_INITIAL_HEALTH;
    player1_state.lives = PLAYER_INITIAL_LIVES;
    player1_state.moveDirection = 0.f; // Asegurar estado inicial quieto
    player1_state.wantsToShoot = false;

    player2_state.position = { static_cast<float>(SERVER_MAP_WIDTH) - 100.f - GAME_ROOM_PLAYER_WIDTH, static_cast<float>(SERVER_MAP_WIDTH) / 2.f };
    player2_state.health = PLAYER_INITIAL_HEALTH;
    player2_state.lives = PLAYER_INITIAL_LIVES;
    player2_state.moveDirection = 0.f;
    player2_state.wantsToShoot = false;
}

void GameRoom::run() {
    running_flag = true; // Marcar la sala como en ejecución
    std::cout << "[GameRoom " << id << "] Iniciando bucle de juego." << std::endl;

    sf::Clock frameClock; // Para calcular deltaTime para updateGameState

    // Enviar estado inicial una vez al comenzar la sala
    sendGameStateToClients();
    std::cout << "[GameRoom " << id << "] Estado inicial enviado a los clientes." << std::endl;

    while (running_flag.load()) { // Usar .load() para leer atomics
        float deltaTime = frameClock.restart().asSeconds(); // Tiempo desde el último frame/tick lógico

        // Los inputs se procesan directamente en processUdpPacket y actualizan playerN_state.moveDirection

        // Lógica del juego a una tasa fija (gameTickInterval)
        if (gameLogicClock.getElapsedTime() >= gameTickInterval) {
            // Calcular un deltaTime específico para la lógica del juego si es necesario,

            updateGameState(gameTickInterval.asSeconds());
            sendGameStateToClients();          
            gameLogicClock.restart();
        }

        // Pausa para no consumir 100% CPU y ceder tiempo.
        // El valor exacto puede ajustarse. Si el ThreadPool maneja esto,
        // un sleep aquí asegura que esta tarea no monopolice un worker.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // La llave del while estaba mal puesta antes.
    std::cout << "[GameRoom " << id << "] Bucle de juego detenido." << std::endl;
}

void GameRoom::stop() {
    running_flag = false; // Esto detendrá el bucle en run()
    std::cout << "[GameRoom " << id << "] Solicitud de parada recibida." << std::endl;
}

void GameRoom::processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet) {
    if (!running_flag.load()) return;

    GameRoomPacketType packetType;
    if (!(packet >> packetType)) {
        std::cerr << "[GameRoom " << id << "] Error al leer GameRoomPacketType." << std::endl;
        return;
    }

    if (packetType == GameRoomPacketType::GR_C_PLAYER_INPUT) {
        float moveDirInput;
        bool shootInput; // Aunque no la usemos para disparar aún, la leemos

        if (packet >> moveDirInput >> shootInput) {
            std::string received_from_player = "UNKNOWN"; // Para el log


            // Determinar qué jugador envió el input y actualizar su estado de input
            if (remoteAddress == player1_address && remotePort == player1_port) {
                received_from_player = "P1";

                player1_state.moveDirection = moveDirInput;
                player1_state.wantsToShoot = shootInput; // Guardar aunque no se use aún
                 //std::cout << "[GameRoom " << id << "] Input de P1: moveDir=" << moveDirInput << std::endl;
            }
            else if (remoteAddress == player2_address && remotePort == player2_port) {
                received_from_player = "P2";

                player2_state.moveDirection = moveDirInput;
                player2_state.wantsToShoot = shootInput;
                 //std::cout << "[GameRoom " << id << "] Input de P2: moveDir=" << moveDirInput << std::endl;
            }
            else {
                std::cerr << "[GameRoom " << id << "] Paquete GR_C_PLAYER_INPUT de un desconocido: "
                    << remoteAddress.toString() << ":" << remotePort << std::endl;
            }

            /*std::cout << "[GAMEROOM " << id << " INPUT_PROCESSED from " << received_from_player
                << "] InputMoveDir: " << moveDirInput
                << " | P1_state.moveDir: " << player1_state.moveDirection
                << " | P2_state.moveDir: " << player2_state.moveDirection << std::endl;*/

        }
        else {
            std::cerr << "[GameRoom " << id << "] Error al leer datos de GR_C_PLAYER_INPUT." << std::endl;
        }
    }
    else {
        std::cerr << "[GameRoom " << id << "] Tipo de paquete UDP desconocido: " << static_cast<int>(packetType) << std::endl;
    }
}

// Nueva función para encapsular la lógica de actualización de un jugador
void GameRoom::updatePlayerState(PlayerState& playerstate, float deltaTime) {
    // Aplicar movimiento horizontal
    playerstate.position.x += playerstate.moveDirection * PLAYER_SERVER_SPEED * deltaTime;

    // Lógica de límites del mapa (simple, solo horizontal por ahora)
    if (playerstate.position.x < 0.f) {
        playerstate.position.x = 0.f;
    }
    if (playerstate.position.x + GAME_ROOM_PLAYER_WIDTH > SERVER_MAP_WIDTH) { // GAME_ROOM_PLAYER_WIDTH debe estar definido
        playerstate.position.x = SERVER_MAP_WIDTH - GAME_ROOM_PLAYER_WIDTH;
    }

    // Aquí iría la lógica de gravedad, salto, colisiones con plataformas del lado del servidor
    // Por ahora, solo movimiento horizontal básico.
    // playerstate.velocity.y += GRAVITY_SERVER * deltaTime;
    // playerstate.position.y += playerstate.velocity.y * deltaTime;
    // ... colisiones ...
}

void GameRoom::updateGameState(float deltaTime) { // Ahora recibe deltaTime
    if (!running_flag.load()) return;
    // LOG CRÍTICO AQUÍ:
   /* std::cout << "[GAMEROOM " << id << " UPDATE_GAME_STATE_START] P1_moveDir: " << player1_state.moveDirection
        << " | P2_moveDir: " << player2_state.moveDirection << std::endl;*/


    // Actualizar estado de cada jugador basado en sus inputs (moveDirection)
    updatePlayerState(player1_state, deltaTime);
    updatePlayerState(player2_state, deltaTime);

    // Lógica de disparo (cuando se implemente)
    // if (player1_state.wantsToShoot) { /* crear bala, etc. */ player1_state.wantsToShoot = false; }
    // if (player2_state.wantsToShoot) { /* crear bala, etc. */ player2_state.wantsToShoot = false; } f
    
    // Actualizar balas, comprobar colisiones de balas, daño, etc.
}

void GameRoom::sendGameStateToClients() {
    if (!running_flag.load()) return;

    sf::Packet gameStatePacket;
    gameStatePacket << GameRoomPacketType::GR_S_GAME_STATE;

    gameStatePacket << player1_state.position.x << player1_state.position.y << player1_state.health << player1_state.lives;
    gameStatePacket << player2_state.position.x << player2_state.position.y << player2_state.health << player2_state.lives;

    //std::cout << "---------[GAMEROOM " << id << " SENDING_DATA] P1_X: " << player1_state.position.x<< " | P2_X: " << player2_state.position.x << std::endl;

    if (game_socket.send(gameStatePacket, player1_address, player1_port) != sf::Socket::Status::Done) {
        std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 1 (" << player1_address.toString() << ":" << player1_port << ")" << std::endl;
    }

    if (game_socket.send(gameStatePacket, player2_address, player2_port) != sf::Socket::Status::Done) {
        std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 2 (" << player2_address.toString() << ":" << player2_port << ")" << std::endl;
    }
    // std::cout << "[GameRoom " << id << "] Estado de juego enviado a P1(" << player1_state.position.x << ") P2(" << player2_state.position.x << ")" << std::endl;
}