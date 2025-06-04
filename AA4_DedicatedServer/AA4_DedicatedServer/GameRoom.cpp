#include "GameRoom.h"
#include <iostream>
#include <thread>
#include <SFML/System/Clock.hpp> 
#include <SFML/System/Time.hpp>  
#include <algorithm> 
#include <fstream>

// Función para cargar el mapa en el servidor
std::vector<sf::RectangleShape> GameRoom::loadServerMap(const std::string& filename) {
    std::vector<sf::RectangleShape> platforms_map;
    std::ifstream inputFile(filename);
    std::string line;
    float y_coord = 0;

    if (!inputFile.is_open()) {
        std::cerr << "[GameRoom " << id << "] Error: No se pudo abrir el archivo del mapa del servidor: " << filename << std::endl;
        sf::RectangleShape floor;
        floor.setSize({ static_cast<float>(SERVER_MAP_WIDTH), TILE_SIZE });
        floor.setPosition({ 0.f, static_cast<float>(SERVER_MAP_HEIGHT) - TILE_SIZE });
        platforms_map.push_back(floor);
        std::cerr << "[GameRoom " << id << "] Se añadió un suelo por defecto." << std::endl;
        return platforms_map;
    }

    while (std::getline(inputFile, line)) {
        float x_coord = 0;
        for (char c : line) {
            if (c == 'P') {
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
    std::cout << "[GameRoom " << id << "] Mapa '" << filename << "' cargado con " << platforms_map.size() << " plataformas." << std::endl;
    return platforms_map;
}


// Constructor de GameRoom
GameRoom::GameRoom(const std::string& roomId, sf::UdpSocket& serverSocket, sf::IpAddress p1Addr, unsigned short p1Port, sf::IpAddress p2Addr, unsigned short p2Port, const std::string& mapFilePath)
    : id(roomId),
    running_flag(false),
    game_socket(serverSocket),
    player1_address(p1Addr), player1_port(p1Port),
    player2_address(p2Addr), player2_port(p2Port),
    player1_shoot_cooldown(0.0f),
    player2_shoot_cooldown(0.0f)
{
    m_server_map_platforms = loadServerMap(mapFilePath);

    player1_state.position = { PLAYER_INITIAL_POS_X, PLAYER_INITIAL_POS_Y };
    player1_state.health = PLAYER_INITIAL_HEALTH;
    player1_state.lives = PLAYER_INITIAL_LIVES;
    player1_state.moveDirection = 0.f;
    player1_state.wantsToShoot = false;
    player1_state.jumpRequested = false;
    player1_state.velocity = { 0.f, 0.f };
    player1_state.onGround = false;

    player2_state.position = { PLAYER_INITIAL_POS_X + 300.0f, PLAYER_INITIAL_POS_Y }; // Asegúrate de que esta posición sea válida
    player2_state.health = PLAYER_INITIAL_HEALTH;
    player2_state.lives = PLAYER_INITIAL_LIVES;
    player2_state.moveDirection = 0.f;
    player2_state.wantsToShoot = false;
    player2_state.jumpRequested = false;
    player2_state.velocity = { 0.f, 0.f };
    player2_state.onGround = false;
}

//El bucle principal de ejecución de esta sala de juego. Se ejecuta en un thread del ThreadPool.
void GameRoom::run() {
    running_flag = true;
    std::cout << "[GameRoom " << id << "] Iniciando bucle de juego (con timestep fijo)." << std::endl;

    sendGameStateToClients(); // Envía un estado inicial

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
            // std::cout << "[GameRoom " << id << "] Ticks procesados en el último segundo: " << debugTickCount << std::endl;
            debugTickCount = 0;
            debugSecondTimer.restart();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "[GameRoom " << id << "] Bucle de juego detenido." << std::endl;
}

void GameRoom::stop() {  }

// Procesa un paquete UDP que el DedicatedServer::udpListenLoop ha determinado que pertenece a esta sala.
void GameRoom::processUdpPacket(const sf::IpAddress& remoteAddress, unsigned short remotePort, sf::Packet& packet) {
    if (!running_flag.load()) return;

    GameRoomPacketType packetType;
    if (!(packet >> packetType)) { std::cerr << "[GameRoom " << id << "] Error al leer GameRoomPacketType." << std::endl; return; }

    if (packetType == GameRoomPacketType::GR_C_PLAYER_INPUT) {
        float moveDirInput;
        bool shootInput;
        bool jumpInput;

        if (packet >> moveDirInput >> shootInput >> jumpInput) {
            if (remoteAddress == player1_address && remotePort == player1_port) {
                player1_state.moveDirection = moveDirInput;
                player1_state.wantsToShoot = shootInput;
                player1_state.jumpRequested = jumpInput;
            }
            else if (remoteAddress == player2_address && remotePort == player2_port) {
                player2_state.moveDirection = moveDirInput;
                player2_state.wantsToShoot = shootInput;
                player2_state.jumpRequested = jumpInput;
            }
        }
        else { std::cerr << "[GameRoom " << id << "] Error al leer datos de GR_C_PLAYER_INPUT." << std::endl; }
    }
    else { std::cerr << "[GameRoom " << id << "] Tipo de paquete UDP desconocido: " << static_cast<int>(packetType) << std::endl; }
}

// Actualiza el estado lógico de un jugador individual (movimiento, salto, disparo, colisiones).
void GameRoom::updatePlayerState(PlayerState& playerstate, float deltaTime, float& shootCooldown, int playerId) {
    // Gestión del cooldown de disparo
    if (shootCooldown > 0) {
        shootCooldown -= deltaTime;
        if (shootCooldown < 0) shootCooldown = 0;
    }

    // Crear bala si se solicitó y el cooldown lo permite
    if (playerstate.wantsToShoot && shootCooldown <= 0) {
        sf::Vector2f bulletSpawnPos = playerstate.position;
        sf::Vector2f bulletVelocity;

        // Determinar dirección del disparo. Asumir facingRight si no hay movimiento/velocidad.
        bool facingRight = true; 
        if (playerstate.moveDirection < 0 || playerstate.velocity.x < 0) facingRight = false;
        else if (playerstate.moveDirection > 0 || playerstate.velocity.x > 0) facingRight = true;
       

        if (facingRight) {
            bulletSpawnPos.x += GAME_ROOM_PLAYER_WIDTH;
            bulletVelocity.x = BULLET_SERVER_SPEED;
        }
        else {
            bulletSpawnPos.x -= BULLET_SERVER_RADIUS * 2;
            bulletVelocity.x = -BULLET_SERVER_SPEED;
        }
        bulletSpawnPos.y += GAME_ROOM_PLAYER_HEIGHT / 2.f - BULLET_SERVER_RADIUS; // Centrar verticalmente

        m_server_bullets.emplace_back(bulletSpawnPos, bulletVelocity, BULLET_SERVER_RADIUS, playerId);
        shootCooldown = SHOOT_SERVER_COOLDOWN;
        playerstate.wantsToShoot = false; // Consumir el input de disparo

    }


    // Aplicar salto si se solicitó y el jugador está en el suelo
    if (playerstate.jumpRequested && playerstate.onGround) {
        playerstate.velocity.y = -JUMP_STRENGTH_SERVER;
        playerstate.onGround = false;
    }
    playerstate.jumpRequested = false;

    // Aplicar gravedad
    if (!playerstate.onGround) {
        playerstate.velocity.y += GRAVITY_SERVER * deltaTime;
    }

    // Aplicar movimiento horizontal (establecer velocidad X del jugador según input)
    if (playerstate.moveDirection < 0) { playerstate.velocity.x = -PLAYER_SERVER_SPEED; }
    else if (playerstate.moveDirection > 0) { playerstate.velocity.x = PLAYER_SERVER_SPEED; }
    else { playerstate.velocity.x = 0; }

    // 1. Mover en Y y resolver colisiones verticales
    playerstate.position.y += playerstate.velocity.y * deltaTime;
    playerstate.onGround = false;
    sf::FloatRect playerBoundsY(playerstate.position, { GAME_ROOM_PLAYER_WIDTH, GAME_ROOM_PLAYER_HEIGHT });
    for (const auto& platform : m_server_map_platforms) {
        std::optional<sf::FloatRect> intersection = playerBoundsY.findIntersection(platform.getGlobalBounds());
        if (intersection) {
            if (playerstate.velocity.y > 0) { playerstate.position.y = platform.getPosition().y - GAME_ROOM_PLAYER_HEIGHT; playerstate.onGround = true; playerstate.velocity.y = 0; }
            else if (playerstate.velocity.y < 0) { playerstate.position.y = platform.getPosition().y + platform.getSize().y; playerstate.velocity.y = 0; }
            break;
        }
    }

    // 2. Mover en X y resolver colisiones horizontales
    playerstate.position.x += playerstate.velocity.x * deltaTime;
    sf::FloatRect playerBoundsX(playerstate.position, { GAME_ROOM_PLAYER_WIDTH, GAME_ROOM_PLAYER_HEIGHT });
    for (const auto& platform : m_server_map_platforms) {
        std::optional<sf::FloatRect> intersection = playerBoundsX.findIntersection(platform.getGlobalBounds());
        if (intersection) {
            if (playerstate.velocity.x > 0) { playerstate.position.x = platform.getPosition().x - GAME_ROOM_PLAYER_WIDTH; }
            else if (playerstate.velocity.x < 0) { playerstate.position.x = platform.getPosition().x + platform.getSize().x; }
            playerstate.velocity.x = 0; break;
        }
    }

    // 3. Límites de pantalla (X)
    if (playerstate.position.x < 0.f) { playerstate.position.x = 0.f; playerstate.velocity.x = 0.f; }
    if (playerstate.position.x + GAME_ROOM_PLAYER_WIDTH > SERVER_MAP_WIDTH) { playerstate.position.x = SERVER_MAP_WIDTH - GAME_ROOM_PLAYER_WIDTH; playerstate.velocity.x = 0.f; }

    // 4. Límite de caída (Y)
    if (playerstate.position.y > SERVER_MAP_HEIGHT + 100.f) {
        std::cout << "[GameRoom " << id << "] Jugador " << playerId << " cayó del mapa, reapareciendo." << std::endl;
        playerstate.position = { PLAYER_INITIAL_POS_X, PLAYER_INITIAL_POS_Y };
        playerstate.velocity = { 0.f, 0.f }; playerstate.health--;
        if (playerstate.health <= 0) { playerstate.lives--; playerstate.health = PLAYER_INITIAL_HEALTH; if (playerstate.lives < 0) playerstate.lives = 0; }
        playerstate.onGround = true;
    }
}

// Actualizar las balas en el servidor y detectar colisiones
void GameRoom::updateBulletsServer(float deltaTime) {
    for (auto& bullet : m_server_bullets) {
        if (bullet.isActive) {
            bullet.position += bullet.velocity * deltaTime;

            sf::FloatRect bulletBounds(bullet.position, { bullet.radius * 2, bullet.radius * 2 });
            for (const auto& platform : m_server_map_platforms) {
                if (bulletBounds.findIntersection(platform.getGlobalBounds())) {
                    bullet.isActive = false; break;
                }
            }

            PlayerState* targetPlayer = nullptr;
            if (bullet.ownerPlayerId == 1) targetPlayer = &player2_state;
            else if (bullet.ownerPlayerId == 2) targetPlayer = &player1_state;

            if (targetPlayer && targetPlayer->health > 0) {
                sf::FloatRect targetPlayerBounds(targetPlayer->position, { GAME_ROOM_PLAYER_WIDTH, GAME_ROOM_PLAYER_HEIGHT });
                if (bulletBounds.findIntersection(targetPlayerBounds)) {
                    bullet.isActive = false; targetPlayer->health--;
                    std::cout << "[GameRoom " << id << "] Bala de jugador " << bullet.ownerPlayerId << " golpeó a jugador. Salud restante: " << targetPlayer->health << std::endl;
                }
            }

            if (bullet.position.x < -100 || bullet.position.x > SERVER_MAP_WIDTH + 100 ||
                bullet.position.y < -100 || bullet.position.y > SERVER_MAP_HEIGHT + 100) {
                bullet.isActive = false;
            }
        }
    }
    m_server_bullets.erase(std::remove_if(m_server_bullets.begin(), m_server_bullets.end(),
        [](const ServerBullet& b) { return !b.isActive; }),
        m_server_bullets.end());
}

//Orquesta la actualización de todos los elementos de la lógica del juego para esta sala
void GameRoom::updateGameState(float deltaTime) {
    if (!running_flag.load()) return;
    updatePlayerState(player1_state, deltaTime, player1_shoot_cooldown, 1);
    updatePlayerState(player2_state, deltaTime, player2_shoot_cooldown, 2);
    updateBulletsServer(deltaTime);
}

//Serializa el estado actual del juego(posiciones de jugadores, salud, vidas, velocidad, estado en el suelo, y estado de las balas)
//  y lo envía a ambos clientes conectados a esta sala.
void GameRoom::sendGameStateToClients() {
    if (!running_flag.load()) return;

    sf::Packet gameStatePacket;
    gameStatePacket << GameRoomPacketType::GR_S_GAME_STATE;

    gameStatePacket << player1_state.position.x << player1_state.position.y << player1_state.health << player1_state.lives
        << player1_state.velocity.x << player1_state.velocity.y << player1_state.onGround;
    gameStatePacket << player2_state.position.x << player2_state.position.y << player2_state.health << player2_state.lives
        << player2_state.velocity.x << player2_state.velocity.y << player2_state.onGround;

    gameStatePacket << static_cast<int>(m_server_bullets.size());
    for (const auto& bullet : m_server_bullets) {
        gameStatePacket << bullet.position.x << bullet.position.y << bullet.velocity.x << bullet.velocity.y
            << bullet.radius << bullet.isActive << bullet.ownerPlayerId;
    }

    if (game_socket.send(gameStatePacket, player1_address, player1_port) != sf::Socket::Status::Done) {  }
    if (game_socket.send(gameStatePacket, player2_address, player2_port) != sf::Socket::Status::Done) {  }
}