#include "GameRoom.h"
#include <iostream>
#include <thread>
#include <SFML/System/Clock.hpp> 
#include <SFML/System/Time.hpp>  
#include <algorithm> // Para std::min/max en loadServerMap
#include <fstream>

// Las constantes GAME_ROOM_PLAYER_WIDTH, GRAVITY_SERVER, JUMP_STRENGTH_SERVER
// están definidas en GameRoom.h ahora.

GameRoom::GameRoom(const std::string& roomId, sf::UdpSocket& serverSocket, sf::IpAddress p1Addr, unsigned short p1Port, sf::IpAddress p2Addr, unsigned short p2Port, const std::string& mapFilePath)
    : id(roomId),
    running_flag(false),
    game_socket(serverSocket),
    player1_address(p1Addr), player1_port(p1Port),
    player2_address(p2Addr), player2_port(p2Port)
{
    // Cargar el mapa para la simulación del servidor
    m_server_map_platforms = loadServerMap(mapFilePath); // <--- NUEVO

    // Inicializar estados de jugadores
    // Posición inicial sobre el suelo (asumiendo que hay un suelo en el mapa o el fallback lo crea)
    player1_state.position = { PLAYER_INITIAL_POS_X, PLAYER_INITIAL_POS_Y };
    player1_state.health = PLAYER_INITIAL_HEALTH;
    player1_state.lives = PLAYER_INITIAL_LIVES;
    player1_state.moveDirection = 0.f;
    player1_state.wantsToShoot = false;
    player1_state.jumpRequested = false;
    player1_state.velocity = { 0.f, 0.f };
    player1_state.onGround = false;

    player2_state.position = { PLAYER_INITIAL_POS_X + 300,PLAYER_INITIAL_POS_Y };
    player2_state.health = PLAYER_INITIAL_HEALTH;
    player2_state.lives = PLAYER_INITIAL_LIVES;
    player2_state.moveDirection = 0.f;
    player2_state.wantsToShoot = false;
    player2_state.jumpRequested = false;
    player2_state.velocity = { 0.f, 0.f };
    player2_state.onGround = false;

    // Realizar un tick inicial para establecer onGround si los jugadores están sobre el suelo
    // Esto es opcional, pero ayuda a que el estado inicial sea consistente.
    // updatePlayerState(player1_state, 0.0f); // deltaTime 0 para solo forzar colisión inicial
    // updatePlayerState(player2_state, 0.0f);
}
// <--- NUEVO: Función para cargar el mapa en el servidor

// Función para cargar el mapa en el servidor (copia de la lógica del cliente)
std::vector<sf::RectangleShape> GameRoom::loadServerMap(const std::string& filename) {
    std::vector<sf::RectangleShape> platforms_map;
    std::ifstream inputFile(filename);
    std::string line;
    float y_coord = 0;

    if (!inputFile.is_open()) {
        std::cerr << "[GameRoom] Error: No se pudo abrir el archivo del mapa del servidor: " << filename << std::endl;
        // Fallback: Añadir un suelo por defecto si el mapa no se carga
        sf::RectangleShape floor;
        floor.setSize({ static_cast<float>(SERVER_MAP_WIDTH), TILE_SIZE });
        floor.setPosition({ 0.f, static_cast<float>(SERVER_MAP_HEIGHT) - TILE_SIZE });
        platforms_map.push_back(floor);
        std::cerr << "[GameRoom] Se añadió un suelo por defecto." << std::endl;
        return platforms_map;
    }

    while (std::getline(inputFile, line)) {
        float x_coord = 0;
        for (char c : line) {
            if (c == 'P') { // 'P' representa una plataforma
                sf::RectangleShape platform;
                platform.setSize({ TILE_SIZE, TILE_SIZE });
                platform.setPosition({ x_coord, y_coord });
                platforms_map.push_back(platform);
            }
            x_coord += TILE_SIZE;
        }
        y_coord += TILE_SIZE;
    }
    inputFile.close();
    std::cout << "[GameRoom] Mapa '" << filename << "' cargado con " << platforms_map.size() << " plataformas." << std::endl;
    return platforms_map;
}



void GameRoom::run() {
    running_flag = true;
    std::cout << "[GameRoom " << id << "] Iniciando bucle de juego (con timestep fijo mejorado)." << std::endl;

    // Puedes enviar un estado inicial aquí, pero el bucle de tick lo hará de todas formas
    // sendGameStateToClients(); 

    sf::Clock wallClock;
    sf::Time accumulator = sf::Time::Zero;
    sf::Clock debugSecondTimer;
    int debugTickCount = 0;
    debugSecondTimer.restart();

    while (running_flag.load()) {
        accumulator += wallClock.restart();

        bool state_was_updated_in_this_outer_loop = false;
        while (accumulator >= gameTickInterval) {
            updateGameState(gameTickInterval.asSeconds());
            accumulator -= gameTickInterval;
            state_was_updated_in_this_outer_loop = true;
            debugTickCount++;
        }

        if (state_was_updated_in_this_outer_loop) {
            sendGameStateToClients();
        }

        if (debugSecondTimer.getElapsedTime().asSeconds() >= 1.0f) {
            std::cout << "[GameRoom " << id << "] Ticks procesados en el último segundo: " << debugTickCount << std::endl;
            debugTickCount = 0;
            debugSecondTimer.restart();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "[GameRoom " << id << "] Bucle de juego detenido." << std::endl;
}

void GameRoom::stop() {
    running_flag = false;
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
        bool shootInput;
        bool jumpInput; // <--- NUEVO: Leer input de salto

        if (packet >> moveDirInput >> shootInput >> jumpInput) { // <--- MODIFICADO: Leer input de salto
            if (remoteAddress == player1_address && remotePort == player1_port) {
                player1_state.moveDirection = moveDirInput;
                player1_state.wantsToShoot = shootInput;
                player1_state.jumpRequested = jumpInput; // <--- NUEVO: Almacenar solicitud de salto
            }
            else if (remoteAddress == player2_address && remotePort == player2_port) {
                player2_state.moveDirection = moveDirInput;
                player2_state.wantsToShoot = shootInput;
                player2_state.jumpRequested = jumpInput; // <--- NUEVO: Almacenar solicitud de salto
            }
            // Log input for debugging:
            // std::cout << "[GameRoom " << id << "] INPUT_PROCESSED. P1: (" << player1_state.moveDirection << ", " << player1_state.jumpRequested << ") P2: (" << player2_state.moveDirection << ", " << player2_state.jumpRequested << ")" << std::endl;
        }
        else {
            std::cerr << "[GameRoom " << id << "] Error al leer datos de GR_C_PLAYER_INPUT." << std::endl;
        }
    }
    else {
        std::cerr << "[GameRoom " << id << "] Tipo de paquete UDP desconocido: " << static_cast<int>(packetType) << std::endl;
    }
}

// <--- MODIFICADO: Lógica de física autoritativa del servidor con colisiones de plataforma// Lógica de física autoritativa del servidor con colisiones de plataforma
void GameRoom::updatePlayerState(PlayerState& playerstate, float deltaTime) {
    // Aplicar salto si se solicitó y el jugador está en el suelo
    if (playerstate.jumpRequested && playerstate.onGround) {
        playerstate.velocity.y = -JUMP_STRENGTH_SERVER;
        playerstate.onGround = false;
    }
    // Consumir la solicitud de salto para que no salte continuamente si el cliente envía el flag constantemente.
    playerstate.jumpRequested = false;

    // Aplicar gravedad
    if (!playerstate.onGround) {
        playerstate.velocity.y += GRAVITY_SERVER * deltaTime;
    }

    // Aplicar movimiento horizontal
    if (playerstate.moveDirection < 0) {
        playerstate.velocity.x = -PLAYER_SERVER_SPEED;
    }
    else if (playerstate.moveDirection > 0) {
        playerstate.velocity.x = PLAYER_SERVER_SPEED;
    }
    else {
        playerstate.velocity.x = 0;
    }


    // --- Lógica de colisión Y-then-X (copiada del cliente, adaptada para PlayerState) ---

    // 1. Mover en Y y resolver colisiones verticales
    playerstate.position.y += playerstate.velocity.y * deltaTime;
    playerstate.onGround = false; // Asumir no en el suelo hasta que una colisión lo confirme
    // Usando sf::Vector2f para la posición y el tamaño
    sf::FloatRect playerBoundsY(playerstate.position, { GAME_ROOM_PLAYER_WIDTH, GAME_ROOM_PLAYER_HEIGHT });
   

    for (const auto& platform : m_server_map_platforms) {
        std::optional<sf::FloatRect> intersection = playerBoundsY.findIntersection(platform.getGlobalBounds());
        if (intersection) {
            if (playerstate.velocity.y > 0) { // Cayendo y choca con la parte superior de la plataforma
                playerstate.position.y = platform.getPosition().y - GAME_ROOM_PLAYER_HEIGHT;
                playerstate.onGround = true;
                playerstate.velocity.y = 0;
            }
            else if (playerstate.velocity.y < 0) { // Saltando y choca con la parte inferior de la plataforma
                playerstate.position.y = platform.getPosition().y + platform.getSize().y;
                playerstate.velocity.y = 0;
            }
            // Importante: En un sistema de colisiones más complejo, no se usaría 'break'
            // y se resolverían todas las penetraciones. Para esta configuración simple,
            // si solo hay una colisión vertical significativa por tick, está bien.
            // La posición Y ya está ajustada, así que playerstate.position.y es la nueva base para X.
            break;
        }
    }


    // 2. Mover en X y resolver colisiones horizontales
    playerstate.position.x += playerstate.velocity.x * deltaTime;
    sf::FloatRect playerBoundsX(playerstate.position, { GAME_ROOM_PLAYER_WIDTH, GAME_ROOM_PLAYER_HEIGHT });

    for (const auto& platform : m_server_map_platforms) {
        std::optional<sf::FloatRect> intersection = playerBoundsX.findIntersection(platform.getGlobalBounds());
        if (intersection) {
            if (playerstate.velocity.x > 0) { // Moviéndose a la derecha, choca con el lado izquierdo de la plataforma
                playerstate.position.x = platform.getPosition().x - GAME_ROOM_PLAYER_WIDTH;
            }
            else if (playerstate.velocity.x < 0) { // Moviéndose a la izquierda, choca con el lado derecho de la plataforma
                playerstate.position.x = platform.getPosition().x + platform.getSize().x;
            }
            playerstate.velocity.x = 0; // Detener movimiento horizontal
            // Similar al eje Y, se asume que se resuelve la colisión principal.
            break;
        }
    }

    // 3. Límites de pantalla (X)
    if (playerstate.position.x < 0.f) {
        playerstate.position.x = 0.f;
        playerstate.velocity.x = 0.f;
    }
    if (playerstate.position.x + GAME_ROOM_PLAYER_WIDTH > SERVER_MAP_WIDTH) {
        playerstate.position.x = SERVER_MAP_WIDTH - GAME_ROOM_PLAYER_WIDTH;
        playerstate.velocity.x = 0.f;
    }

    // 4. Límite de caída (Y) - si el jugador cae por debajo del mapa
    if (playerstate.position.y > SERVER_MAP_HEIGHT + 100.f) { // Un poco por debajo para que no se active de inmediato al caer de una plataforma
        std::cout << "[GameRoom " << id << "] Jugador cayó del mapa, reapareciendo." << std::endl;
        playerstate.position = { 100.f, static_cast<float>(SERVER_MAP_HEIGHT) - TILE_SIZE - GAME_ROOM_PLAYER_HEIGHT }; // Punto de reaparición
        playerstate.velocity = { 0.f, 0.f };
        playerstate.health--; // Ejemplo: pierde salud
        if (playerstate.health <= 0) {
            playerstate.lives--; // Ejemplo: pierde una vida
            playerstate.health = PLAYER_INITIAL_HEALTH; // Restablecer salud
            if (playerstate.lives < 0) playerstate.lives = 0;
        }
        playerstate.onGround = true; // Asumir en el suelo después de reaparecer
    }
}


void GameRoom::updateGameState(float deltaTime) {
    if (!running_flag.load()) return;

    updatePlayerState(player1_state, deltaTime);
    updatePlayerState(player2_state, deltaTime);
}

void GameRoom::sendGameStateToClients() {
    if (!running_flag.load()) return;

    sf::Packet gameStatePacket;
    gameStatePacket << GameRoomPacketType::GR_S_GAME_STATE;

    // Incluir posición, salud, vidas, VELOCIDAD y ONGROUND para el Jugador 1
    gameStatePacket << player1_state.position.x << player1_state.position.y << player1_state.health << player1_state.lives
        << player1_state.velocity.x << player1_state.velocity.y << player1_state.onGround; // <--- MODIFICADO: Añadir velocidad y onGround

    // Incluir posición, salud, vidas, VELOCIDAD y ONGROUND para el Jugador 2
    gameStatePacket << player2_state.position.x << player2_state.position.y << player2_state.health << player2_state.lives
        << player2_state.velocity.x << player2_state.velocity.y << player2_state.onGround; // <--- MODIFICADO: Añadir velocidad y onGround

    if (game_socket.send(gameStatePacket, player1_address, player1_port) != sf::Socket::Status::Done) {
        // std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 1 (" << player1_address.toString() << ":" << player1_port << ")" << std::endl;
    }

    if (game_socket.send(gameStatePacket, player2_address, player2_port) != sf::Socket::Status::Done) {
        // std::cerr << "[GameRoom " << id << "] Error enviando estado a Jugador 2 (" << player2_address.toString() << ":" << player2_port << ")" << std::endl;
    }
}