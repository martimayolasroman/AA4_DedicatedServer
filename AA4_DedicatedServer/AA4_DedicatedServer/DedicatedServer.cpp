// DedicatedServer.cpp
#include "DedicatedServer.h"
#include <iostream>
#include <optional> 

// Definición de tipos de paquete para la comunicación Admin (Matchmaking -> DedicatedServer)

enum  AdminPacketType  { 
    NOTIFY_NEW_GAME = 200,
  
    ADMIN_UNKNOWN = -1
};


inline sf::Packet& operator<<(sf::Packet& packet, AdminPacketType type) {
    return packet << static_cast<int>(type);
}
inline sf::Packet& operator>>(sf::Packet& packet, AdminPacketType& type) {
    int temp;
    if (packet >> temp) {
        type = static_cast<AdminPacketType>(temp);
    }
    else {
        type = AdminPacketType::ADMIN_UNKNOWN; // Valor por defecto en caso de error
    }
    return packet;
}


DedicatedServer::DedicatedServer(unsigned short gamePort, unsigned short adminPort, size_t num_pool_threads)
    : game_udp_port_val(gamePort),
    admin_tcp_port_val(adminPort),
    server_running_flag(false),
    thread_pool_(num_pool_threads) { 

    if (game_udp_socket.bind(game_udp_port_val) != sf::Socket::Status::Done) {
        std::cerr << "[DedicatedServer] Error al enlazar socket UDP al puerto " << game_udp_port_val << std::endl;
        throw std::runtime_error("Failed to bind game UDP socket.");
    }
    game_udp_socket.setBlocking(false);
    std::cout << "[DedicatedServer] Socket UDP de juego escuchando en el puerto " << game_udp_port_val << std::endl;

    if (admin_listener.listen(admin_tcp_port_val) != sf::Socket::Status::Done) {
        std::cerr << "[DedicatedServer] Error al escuchar en el puerto TCP de admin " << admin_tcp_port_val << std::endl;
        throw std::runtime_error("Failed to listen on admin TCP port.");
    }
    std::cout << "[DedicatedServer] Puerto TCP de admin escuchando en " << admin_tcp_port_val << std::endl;
    std::cout << "[DedicatedServer] ThreadPool inicializado con " << num_pool_threads << " threads." << std::endl;
}

DedicatedServer::~DedicatedServer() {
    stop();
}

void DedicatedServer::run() {
    server_running_flag = true;
    // admin_thread_ ahora lanzará el bucle que acepta y luego maneja la conexión.
    admin_thread = std::thread(&DedicatedServer::adminServiceLoop, this);
    udp_listen_thread = std::thread(&DedicatedServer::udpListenLoop, this);

    std::cout << "[DedicatedServer] Servidor Dedicado en ejecución." << std::endl;

    // Esperar a que los threads terminen solo si el servidor se detiene explícitamente
    // o si terminan por alguna otra razón.
    if (admin_thread.joinable()) admin_thread.join();
    if (udp_listen_thread.joinable()) udp_listen_thread.join();

    std::cout << "[DedicatedServer] run() terminando (esto es normal si los threads han terminado)." << std::endl;
}

void DedicatedServer::stop() {
    if (!server_running_flag.exchange(false)) { // Si ya estaba false, no hacer nada
        return;
    }
    std::cout << "[DedicatedServer] Iniciando proceso de detención..." << std::endl;

    // 1. Cerrar el listener para no aceptar nuevas conexiones de admin
    admin_listener.close();
    std::cout << "[DedicatedServer] Admin listener cerrado." << std::endl;

    // 2. El admin_thread_ (que ejecuta adminServiceLoop) debería terminar
    //    porque server_running_flag_ es false y/o accept() fallará.
    //    Si está en handleActiveMatchmakingConnection, server_running_flag_ también lo detendrá.
    if (admin_thread.joinable()) {
        admin_thread.join();
    }
    std::cout << "[DedicatedServer] Thread de servicio de admin detenido." << std::endl;

    // 3. Desvincular el socket UDP para detener la recepción y liberar el puerto
    game_udp_socket.unbind();
    std::cout << "[DedicatedServer] Socket UDP desvinculado." << std::endl;

    // 4. El udp_listen_thread_ debería terminar porque server_running_flag_ es false
    //    y/o game_udp_socket_.receive() fallará.
    if (udp_listen_thread.joinable()) {
        udp_listen_thread.join();
    }
    std::cout << "[DedicatedServer] Thread de escucha UDP detenido." << std::endl;

    // 5. Señalar a todas las GameRooms activas que se detengan
    {
        std::lock_guard<std::mutex> lock(rooms_mutex);
        std::cout << "[DedicatedServer] Señalando stop a " << active_game_rooms.size() << " GameRooms activas..." << std::endl;
        for (GameRoom* room : active_game_rooms) {
            if (room) room->stop();
        }
    }

    // 6. Detener el ThreadPool. Esto esperará a que las tareas de las GameRoom terminen.
    std::cout << "[DedicatedServer] Deteniendo ThreadPool..." << std::endl;
    thread_pool_.stop();
    std::cout << "[DedicatedServer] ThreadPool detenido." << std::endl;

    // 7. Limpiar memoria de las GameRooms
    {
        std::lock_guard<std::mutex> lock(rooms_mutex);
        std::cout << "[DedicatedServer] Liberando memoria de GameRooms..." << std::endl;
        for (GameRoom* room : active_game_rooms) {
            delete room;
        }
        active_game_rooms.clear();
    }
    std::cout << "[DedicatedServer] Memoria de GameRooms liberada." << std::endl;

    // 8. Limpiar el mapa de clientes
    {
        std::lock_guard<std::mutex> map_lock(client_map_mutex);
        client_to_room_map.clear();
    }
    std::cout << "[DedicatedServer] Servidor Dedicado detenido completamente." << std::endl;
}

void DedicatedServer::adminServiceLoop() {
    std::cout << "[DedicatedServer-AdminService] Iniciado. Esperando conexión del MatchmakingService..." << std::endl;
    while (server_running_flag.load()) {
        sf::TcpSocket matchmaking_connection_socket; // Socket para la conexión entrante
        if (admin_listener.accept(matchmaking_connection_socket) == sf::Socket::Status::Done) {
            std::cout << "[DedicatedServer-AdminService] MatchmakingService conectado desde: "
                << matchmaking_connection_socket.getRemoteAddress().value()
                << ":" << matchmaking_connection_socket.getRemotePort() << std::endl;

            // Una vez conectado, manejar todas las notificaciones de este cliente
            // hasta que se desconecte o el servidor se detenga.
            handleActiveMatchmakingConnection(std::move(matchmaking_connection_socket));
            // Cuando handleActiveMatchmakingConnection retorne (por desconexión o error),
            // el bucle volverá a esperar una nueva conexión.
            std::cout << "[DedicatedServer-AdminService] Conexión con MatchmakingService terminada. Volviendo a escuchar..." << std::endl;
        }
        else {
            // Accept falló. Si el servidor sigue corriendo, es un problema del listener.
            // Si el servidor se está deteniendo, server_running_flag_ será false y el bucle terminará.
            if (server_running_flag.load()) {
                std::cerr << "[DedicatedServer-AdminService] Error aceptando conexión del MatchmakingService. El listener podría estar cerrado." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1)); // Esperar antes de reintentar
            }
        }
    }
    std::cout << "[DedicatedServer-AdminService] Detenido." << std::endl;
}

void DedicatedServer::handleActiveMatchmakingConnection(sf::TcpSocket matchmaking_socket) {
    // El socket se movió aquí, este thread es ahora su dueño.
    matchmaking_socket.setBlocking(true); // Esperar por notificaciones

    sf::Packet packet;
    while (server_running_flag.load()) {
        packet.clear(); // Limpiar paquete para nueva recepción
        sf::Socket::Status status = matchmaking_socket.receive(packet);

        if (status == sf::Socket::Status::Done) {
            AdminPacketType type = AdminPacketType::ADMIN_UNKNOWN; // Valor por defecto
            if (!(packet >> type)) { // Intenta deserializar el tipo
                std::cerr << "[DedicatedServer-AdminHandler] Error deserializando AdminPacketType." << std::endl;
                // Podría ser un paquete corrupto o una desconexión no limpia.
                break; // Salir del bucle de recepción para este socket
            }

            if (type == AdminPacketType::NOTIFY_NEW_GAME) {
                std::string roomId_str, p1Ip_str, p2Ip_str;
                unsigned short p1Port_val = 0, p2Port_val = 0;

                if (packet >> roomId_str >> p1Ip_str >> p1Port_val >> p2Ip_str >> p2Port_val) {
                    NewGameNotification notification;
                    notification.roomId = roomId_str;
                    notification.player1Ip = sf::IpAddress::resolve(p1Ip_str);
                    notification.player1UdpPort = p1Port_val;
                    notification.player2Ip = sf::IpAddress::resolve(p2Ip_str);
                    notification.player2UdpPort = p2Port_val;

                    if (!notification.player1Ip.has_value() || !notification.player2Ip.has_value()) {
                        std::cerr << "[DedicatedServer-AdminHandler] Error resolviendo IPs para sala " << roomId_str << std::endl;
                        // No se puede crear la sala, pero la conexión con MatchmakingService puede seguir.
                        // Podrías enviar un NACK (Not Acknowledged) al MatchmakingService aquí.
                        continue; // Esperar el siguiente paquete
                    }

                    std::cout << "[DedicatedServer-AdminHandler] Notificación de juego recibida para sala: " << notification.roomId
                        << " | P1: " << notification.player1Ip.value().toString() << ":" << notification.player1UdpPort
                        << " | P2: " << notification.player2Ip.value().toString() << ":" << notification.player2UdpPort << std::endl;

                    GameRoom* new_room = nullptr;
                    try {
                        new_room = new GameRoom(notification.roomId, game_udp_socket,
                            notification.player1Ip.value(), notification.player1UdpPort,
                            notification.player2Ip.value(), notification.player2UdpPort);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "[DedicatedServer-AdminHandler] Excepción creando GameRoom '" << notification.roomId << "': " << e.what() << std::endl;
                        continue; // Esperar el siguiente paquete
                    }

                    if (new_room) {
                        {
                            std::lock_guard<std::mutex> lock(rooms_mutex);
                            active_game_rooms.push_back(new_room);
                        }
                        {
                            std::lock_guard<std::mutex> map_lock(client_map_mutex);
                            client_to_room_map[{notification.player1Ip.value(), notification.player1UdpPort}] = new_room;
                            client_to_room_map[{notification.player2Ip.value(), notification.player2UdpPort}] = new_room;
                        }

                        GameRoom* room_ptr_for_task = new_room;
                        thread_pool_.enqueueTask([room_ptr_for_task]() {
                            if (room_ptr_for_task) {
                                std::cout << "[ThreadPool Task] Iniciando GameRoom " << room_ptr_for_task->getRoomId() << std::endl;
                                room_ptr_for_task->run();
                                std::cout << "[ThreadPool Task] GameRoom " << room_ptr_for_task->getRoomId() << " ha finalizado su ejecución." << std::endl;
                                // Aquí NO se borra room_ptr_for_task. Se borra en DedicatedServer::stop()
                                // o si implementas un mecanismo para que las salas se auto-eliminen de active_game_rooms_.
                            }
                            });
                        std::cout << "[DedicatedServer-AdminHandler] GameRoom '" << new_room->getRoomId() << "' encolada." << std::endl;
                    }
                }
                else { // Fallo al deserializar datos de NOTIFY_NEW_GAME
                    std::cerr << "[DedicatedServer-AdminHandler] Error deserializando datos de NOTIFY_NEW_GAME." << std::endl;
                }
            }
            else if (type == AdminPacketType::ADMIN_UNKNOWN) {
                std::cerr << "[DedicatedServer-AdminHandler] Error severo deserializando AdminPacketType (paquete corrupto o stream finalizado)." << std::endl;
                break; // Probablemente la conexión está mal
            }
            else { // Tipo de paquete Admin desconocido
                std::cerr << "[DedicatedServer-AdminHandler] Tipo de paquete Admin desconocido: " << static_cast<int>(type) << std::endl;
            }
        }
        else if (status == sf::Socket::Status::Disconnected) {
            std::cout << "[DedicatedServer-AdminHandler] MatchmakingService se desconectó." << std::endl;
            break; // Salir del bucle de recepción
        }
        else if (status == sf::Socket::Status::Error) {
            std::cerr << "[DedicatedServer-AdminHandler] Error de socket con MatchmakingService." << std::endl;
            break; // Salir del bucle
        }
        // Si NotReady y es bloqueante, es un error. Si es no bloqueante, sería normal.
        // Pero lo pusimos bloqueante.
    }
    // El bucle while (server_running_flag_.load()) también puede terminar el bucle.
    matchmaking_socket.disconnect(); // Asegurar desconexión al salir del handler
    std::cout << "[DedicatedServer-AdminHandler] Terminada la gestión de la conexión actual con MatchmakingService." << std::endl;
}


void DedicatedServer::udpListenLoop() {
    std::cout << "[DedicatedServer-UDPThread] Escuchando paquetes UDP de juego..." << std::endl;
    sf::Packet received_packet;
    std::optional<sf::IpAddress> sender_ip; // Se inicializará por defecto a None
    unsigned short sender_port = 0;

    while (server_running_flag.load()) {
        received_packet.clear(); // Limpiar para la nueva recepción
        if (game_udp_socket.receive(received_packet, sender_ip, sender_port) == sf::Socket::Status::Done) {
            GameRoom* target_room = nullptr;
            {
                std::lock_guard<std::mutex> lock(client_map_mutex);
                ClientUDPAddress client_addr = { sender_ip.value(), sender_port};
                auto it = client_to_room_map.find(client_addr);
                if (it != client_to_room_map.end()) {
                    target_room = it->second;
                }
            }

            if (target_room && target_room->isRunning()) {
                target_room->processUdpPacket(sender_ip.value(), sender_port, received_packet);
            }
            else {
                // Opcional: Log si el paquete no va a ninguna sala o la sala no está corriendo
                // if (!target_room) {
                //     std::cout << "[DedicatedServer-UDPThread] Paquete UDP de un cliente no asociado: "
                //               << sender_ip.toString() << ":" << sender_port << std::endl;
                // }
            }
        }
        else {
            // Si no es Done y el socket es no bloqueante, fue NotReady.
            // Un pequeño sleep evita un bucle de CPU muy rápido.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::cout << "[DedicatedServer-UDPThread] Detenido." << std::endl;
}