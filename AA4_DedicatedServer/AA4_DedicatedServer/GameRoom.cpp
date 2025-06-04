#include "GameRoom.h"
#include <iostream>
#include <thread> // Para std::this_thread::sleep_for
#include <SFML/System/Clock.hpp> 
#include <SFML/System/Time.hpp>  

// Constantes específicas de GameRoom (si no están ya en PlayerState.h o GameRoom.h)
const float GAME_ROOM_PLAYER_WIDTH = 32.0f * 0.9f; // Ancho del jugador para colisiones en el servidor
const int PLAYER_INITIAL_HEALTH = 5;
const int PLAYER_INITIAL_LIVES = 3;


GameRoom::GameRoom(const std::string& roomId, sf::UdpSocket& serverSocket, sf::IpAddress p1Addr, unsigned short p1Port, sf::IpAddress p2Addr, unsigned short p2Port)
    : id(roomId),
    running_flag(false), // Se pone a true al inicio de run()
    game_socket(serverSocket),
    player1_address(p1Addr), player1_port(p1Port),
    player2_address(p2Addr), player2_port(p2Port)
    // gameTickInterval se inicializa en la declaración en GameRoom.h
{
    /*
    std::cout << "[GameRoom " << id << "] Creada para P1:"
        << player1_address.toString() << ":" << player1_port << " y P2:"
        << player2_address.toString() << ":" << player2_port << std::endl;*/

    // Inicializar estados de jugadores
    player1_state.position = { 100.f, static_cast<float>(SERVER_MAP_WIDTH) / 2.f }; // REVISAR: Y usa SERVER_MAP_WIDTH, ¿es correcto o debería ser una altura?
    player1_state.health = PLAYER_INITIAL_HEALTH;
    player1_state.lives = PLAYER_INITIAL_LIVES;
    player1_state.moveDirection = 0.f;
    player1_state.wantsToShoot = false;

    player2_state.position = { static_cast<float>(SERVER_MAP_WIDTH) - 100.f - GAME_ROOM_PLAYER_WIDTH, static_cast<float>(SERVER_MAP_WIDTH) / 2.f };
    player2_state.health = PLAYER_INITIAL_HEALTH;
    player2_state.lives = PLAYER_INITIAL_LIVES;
    player2_state.moveDirection = 0.f;
    player2_state.wantsToShoot = false;
}

void GameRoom::run() {
    running_flag = true;
    std::cout << "[GameRoom " << id << "] Iniciando bucle de juego (con timestep fijo mejorado)." << std::endl;

    sendGameStateToClients(); // Envía estado inicial una vez
    std::cout << "[GameRoom " << id << "] Estado inicial enviado a los clientes." << std::endl;

    sf::Clock wallClock; // Mide el tiempo real transcurrido entre iteraciones del bucle exterior
    sf::Time accumulator = sf::Time::Zero;
    // gameTickInterval (miembro de la clase) define el paso de tiempo fijo para la simulación

    // Para depurar TPS (Ticks Por Segundo)
    sf::Clock debugSecondTimer;
    int debugTickCount = 0;
    debugSecondTimer.restart();

    while (running_flag.load()) { // Usar .load() para leer atomics de forma segura
        accumulator += wallClock.restart(); // Acumula el tiempo real desde la última iteración

        // Opcional: Limitar el acumulador para evitar una "espiral de la muerte"
        // si el servidor se retrasa demasiado y acumula mucho tiempo.
        // const sf::Time maxAccumulatedTime = sf::seconds(0.25f); // ej. no más de 1/4 de segundo
        // if (accumulator > maxAccumulatedTime) {
        //     accumulator = maxAccumulatedTime;
        // }

        bool state_was_updated_in_this_outer_loop = false;
        while (accumulator >= gameTickInterval) {
            // Los inputs (moveDirection) se actualizan de forma asíncrona en processUdpPacket.
            // Esta parte del bucle consume un paso de tiempo de la simulación.
            updateGameState(gameTickInterval.asSeconds()); // Siempre usa el deltaTime fijo

            accumulator -= gameTickInterval;
            state_was_updated_in_this_outer_loop = true;

            debugTickCount++; // Contar tick procesado
        }

        if (state_was_updated_in_this_outer_loop) {
            sendGameStateToClients(); // Envía el estado más reciente si la simulación avanzó
        }

        // Log de TPS cada segundo
        if (debugSecondTimer.getElapsedTime().asSeconds() >= 1.0f) {
            std::cout << "[GameRoom " << id << "] Ticks procesados en el último segundo: " << debugTickCount << std::endl;
            debugTickCount = 0;
            debugSecondTimer.restart();
        }

        // Ceder tiempo al SO. Un sleep pequeño y fijo es una estrategia simple.
        // Para un control más preciso del timing, se podrían hacer cálculos más complejos,
        // pero esto puede ser suficiente y evita consumir 100% de CPU en un core.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "[GameRoom " << id << "] Bucle de juego detenido." << std::endl;
}

void GameRoom::stop() {
    running_flag = false; // Esto detendrá el bucle en run()
    std::cout << "[GameRoom " << id << "] Solicitud de parada recibida." << std::endl;
}

void GameRoom::processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet) {
    if (!running_flag.load()) return; // No procesar si la sala no está activa

    GameRoomPacketType packetType;
    if (!(packet >> packetType)) { // Usa el operador sobrecargado
        std::cerr << "[GameRoom " << id << "] Error al leer GameRoomPacketType." << std::endl;
        return;
    }

    if (packetType == GameRoomPacketType::GR_C_PLAYER_INPUT) {
        float moveDirInput;
        bool shootInput;

        if (packet >> moveDirInput >> shootInput) {
            // Determinar qué jugador envió el input y actualizar su estado de input
            if (remoteAddress == player1_address && remotePort == player1_port) {
                player1_state.moveDirection = moveDirInput;
                player1_state.wantsToShoot = shootInput;
            }
            else if (remoteAddress == player2_address && remotePort == player2_port) {
                player2_state.moveDirection = moveDirInput;
                player2_state.wantsToShoot = shootInput;
            }
            // El log que tenías "[GAMEROOM ... INPUT_PROCESSED ...]" puede ser útil para depurar si los inputs se están registrando.
            // Coméntalo o descoméntalo según sea necesario.
        }
        else {
            std::cerr << "[GameRoom " << id << "] Error al leer datos de GR_C_PLAYER_INPUT." << std::endl;
        }
    }
    else {
        std::cerr << "[GameRoom " << id << "] Tipo de paquete UDP desconocido: " << static_cast<int>(packetType) << std::endl;
    }
}

void GameRoom::updatePlayerState(PlayerState& playerstate, float deltaTime) {
    // Aplicar movimiento horizontal
    playerstate.position.x += playerstate.moveDirection * PLAYER_SERVER_SPEED * deltaTime;

    // Lógica de límites del mapa (simple, solo horizontal por ahora)
    if (playerstate.position.x < 0.f) {
        playerstate.position.x = 0.f;
    }
    if (playerstate.position.x + GAME_ROOM_PLAYER_WIDTH > SERVER_MAP_WIDTH) {
        playerstate.position.x = SERVER_MAP_WIDTH - GAME_ROOM_PLAYER_WIDTH;
    }
    // Aquí iría la lógica de gravedad, salto, colisiones con plataformas del lado del servidor
    // si no se manejan solo por predicción del cliente y el servidor solo valida/corrige.
}

void GameRoom::updateGameState(float deltaTime) {
    if (!running_flag.load()) return;

    // Log para depurar el estado de los inputs y el deltaTime al inicio de cada tick lógico.
    // Quítalo si es demasiado verboso en producción.
    // std::cout << "[GameRoom " << id << " UPDATE_GAME_STATE] dt=" << deltaTime 
    //           << " P1_moveDir: " << player1_state.moveDirection
    //           << " P2_moveDir: " << player2_state.moveDirection << std::endl;


    updatePlayerState(player1_state, deltaTime);
    updatePlayerState(player2_state, deltaTime);

    // Lógica de disparo:
    // if (player1_state.wantsToShoot) { /* crear bala para P1, etc. */ player1_state.wantsToShoot = false; }
    // if (player2_state.wantsToShoot) { /* crear bala para P2, etc. */ player2_state.wantsToShoot = false; }

    // Actualizar estado de balas, comprobar colisiones de balas, aplicar daño, etc.
    // ...
}

void GameRoom::sendGameStateToClients() {
    if (!running_flag.load()) return;

    sf::Packet gameStatePacket;
    gameStatePacket << GameRoomPacketType::GR_S_GAME_STATE; // Usa el operador sobrecargado

    // Opcional: Enviar el timestamp del servidor para una interpolación más precisa en el cliente.
    // sf::Int64 server_time_milliseconds = ... ; // obtener el tiempo actual del servidor en ms
    // gameStatePacket << server_time_milliseconds;

    gameStatePacket << player1_state.position.x << player1_state.position.y << player1_state.health << player1_state.lives;
    gameStatePacket << player2_state.position.x << player2_state.position.y << player2_state.health << player2_state.lives;

    // Log de datos enviados (comentar si es muy verboso):
    // std::cout << "[GameRoom " << id << " SENDING_DATA] P1_X: " << player1_state.position.x 
    //           << " P2_X: " << player2_state.position.x << std::endl;

    if (game_socket.send(gameStatePacket, player1_address, player1_port) != sf::Socket::Status::Done) {
        // Considerar un log menos frecuente para errores de envío para no llenar la consola.
        // std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 1 (" << player1_address.toString() << ":" << player1_port << ")" << std::endl;
    }

    if (game_socket.send(gameStatePacket, player2_address, player2_port) != sf::Socket::Status::Done) {
        // std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 2 (" << player2_address.toString() << ":" << player2_port << ")" << std::endl;
    }
}