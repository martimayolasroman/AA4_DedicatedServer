#include "DedicatedServer.h"
#include <iostream>


// Definición de tipos de paquete para la comunicación Admin (Matchmaking -> DedicatedServer)
enum  AdminPacketType {
    NOTIFY_NEW_GAME = 200
};

sf::Packet& operator<<(sf::Packet& packet, AdminPacketType type) {
    return packet << static_cast<int>(type);
}
sf::Packet& operator>>(sf::Packet& packet, AdminPacketType& type) {
    int temp;
    packet >> temp;
    type = static_cast<AdminPacketType>(temp);
    return packet;
}




DedicatedServer::DedicatedServer(unsigned short gamePort, unsigned short adminPort, size_t num_pool_threads): game_udp_port_val(gamePort),
admin_tcp_port_val(adminPort),
server_running_flag(false), thread_pool_(num_pool_threads)
{
    // Configurar socket UDP para el juego

    if (game_udp_socket.bind(game_udp_port_val) != sf::Socket::Status::Done) {
        std::cerr << "[DedicatedServer] Error al enlazar socket UDP al puerto " << game_udp_port_val << std::endl;
        // Aquí deberías lanzar una excepción o manejar el error críticamente

    }
    game_udp_socket.setBlocking(false); // Importante para el bucle de escucha UDP
    std::cout << "[DedicatedServer] Socket UDP de juego escuchando en el puerto " << game_udp_port_val << std::endl;

    // Configurar listener TCP para administración
    if (admin_listener.listen(admin_tcp_port_val) != sf::Socket::Status::Done) {
        std::cerr << "[DedicatedServer] Error al escuchar en el puerto TCP de admin " << admin_tcp_port_val << std::endl;
    }
    std::cout << "[DedicatedServer] Puerto TCP de admin escuchando en " << admin_tcp_port_val << std::endl;
    std::cout << "[DedicatedServer] ThreadPool inicializado con " << num_pool_threads << " threads." << std::endl;
}

DedicatedServer::~DedicatedServer()
{

    stop();

}

void DedicatedServer::run()
{

    server_running_flag = true;
    admin_thread = std::thread(&DedicatedServer::adminListenLoop, this);
    udp_listen_thread = std::thread(&DedicatedServer::udpListenLoop, this);

    std::cout << "[DedicatedServer] Servidor Dedicado en ejecución." << std::endl;
    // El hilo principal puede quedarse aquí o hacer otras tareas de mantenimiento.
    // Por ahora, solo esperamos a que se detenga.
    admin_thread.join(); // Esperar a que los threads terminen si se detienen por su cuenta
    udp_listen_thread.join(); // o si server_running_flag los detiene.



}

void DedicatedServer::stop()
{
    if (!server_running_flag.exchange(false)) {
        return;
    }
    std::cout << "[DedicatedServer] Iniciando proceso de detención..." << std::endl;

    admin_listener.close();
    if (admin_thread.joinable()) {
        admin_thread.join();
    }
    std::cout << "[DedicatedServer] Thread de admin detenido." << std::endl;

    game_udp_socket.unbind();
    if (udp_listen_thread.joinable()) {
        udp_listen_thread.join();
    }
    std::cout << "[DedicatedServer] Thread de escucha UDP detenido." << std::endl;

    // Detener todas las GameRooms activas
    // Las tareas de estas salas podrían estar aún en la cola del pool o ejecutándose.
    // Primero, señalamos a las salas que deben detenerse.
    {
        std::lock_guard<std::mutex> lock(rooms_mutex);
        std::cout << "[DedicatedServer] Señalando stop a " << active_game_rooms.size() << " GameRooms activas..." << std::endl;
        for (GameRoom* room : active_game_rooms) {
            room->stop(); // Esto hará que su bucle run() termine
        }
    }

    // Detener el ThreadPool. Esto esperará a que las tareas actuales (incluyendo
    // los bucles run() de las GameRoom que están terminando) finalicen.
    std::cout << "[DedicatedServer] Deteniendo ThreadPool..." << std::endl;
    thread_pool_.stop();
    std::cout << "[DedicatedServer] ThreadPool detenido." << std::endl;

    // Limpiar memoria de las GameRooms
    {
        std::lock_guard<std::mutex> lock(rooms_mutex); // Necesario por si acaso, aunque el pool ya paró
        std::cout << "[DedicatedServer] Liberando memoria de GameRooms..." << std::endl;
        for (GameRoom* room : active_game_rooms) {
            delete room;
        }
        active_game_rooms.clear();
    }
    std::cout << "[DedicatedServer] Memoria de GameRooms liberada." << std::endl;

    {
        std::lock_guard<std::mutex> map_lock(client_map_mutex);
        client_to_room_map.clear();
    }
    std::cout << "[DedicatedServer] Servidor Dedicado detenido completamente." << std::endl;


}

void DedicatedServer::adminListenLoop()
{
    std::cout << "[DedicatedServer-AdminThread] Escuchando nuevas solicitudes de juego..." << std::endl;
    while (server_running_flag) {
        sf::TcpSocket* matchmaking_client = new sf::TcpSocket(); // Puntero para el cliente (MatchmakingService)
        if (admin_listener.accept(*matchmaking_client) == sf::Socket::Status::Done) {
            std::cout << "[DedicatedServer-AdminThread] Conexión del MatchmakingService aceptada desde: "
                << matchmaking_client->getRemoteAddress().value() << std::endl;
            handleNewGameRequest(matchmaking_client); // Pasa el socket del cliente
            // matchmaking_client se cierra y elimina dentro de handleNewGameRequest o después
        }
        else {
            delete matchmaking_client;
            if (server_running_flag) { // Solo mostrar error si no nos estamos deteniendo
                // std::cerr << "[DedicatedServer-AdminThread] Error aceptando conexión del MatchmakingService." << std::endl;
                // Este error puede ocurrir si el listener se cierra mientras accept() está bloqueado.
            }
            break; // Salir del bucle si el listener ya no funciona (por ejemplo, al hacer stop())
        }
    }
    std::cout << "[DedicatedServer-AdminThread] Detenido." << std::endl;

}

void DedicatedServer::handleNewGameRequest(sf::TcpSocket* matchmaking_service_client)
{

    sf::Packet packet;
    // Establecer un timeout para la recepción para no bloquear indefinidamente
    matchmaking_service_client->setBlocking(true); // Temporalmente para asegurar la recepción
    // sf::Time timeout = sf::seconds(5); // Ojo: esto es para socket.connect, no para receive directamente con setBlocking(true)
                                        // Para receive, si es bloqueante, esperará.
                                        // Si es no bloqueante, necesitarías un bucle con selector o timeouts.
                                        // Por simplicidad, si el matchmaking service envía y cierra, esto debería funcionar.


    if (matchmaking_service_client->receive(packet) == sf::Socket::Status::Done) {
        AdminPacketType type;
        std::string roomId_str, p1Ip_str, p2Ip_str;
        unsigned short p1Port_val, p2Port_val;

        // Asumimos el siguiente formato del MatchmakingService:
        // packet << AdminPacketType::NOTIFY_NEW_GAME << roomId << p1Ip.toString() << p1UdpPort << p2Ip.toString() << p2UdpPort;
        if (packet >> type && type == AdminPacketType::NOTIFY_NEW_GAME &&
            packet >> roomId_str >> p1Ip_str >> p1Port_val >> p2Ip_str >> p2Port_val) {

            NewGameNotification notification;
            notification.roomId = roomId_str;
            notification.player1Ip = sf::IpAddress::resolve(p1Ip_str);
            notification.player1UdpPort = p1Port_val;
            notification.player2Ip = sf::IpAddress::resolve(p2Ip_str);
            notification.player2UdpPort = p2Port_val;

            std::cout << "[DedicatedServer] Nueva solicitud de juego recibida para sala: " << notification.roomId
                << " P1: " << notification.player1Ip.value() << ":" << notification.player1UdpPort
                << " P2: " << notification.player2Ip.value() << ":" << notification.player2UdpPort << std::endl;

            // Crear y lanzar la GameRoom
            GameRoom* new_room = nullptr;
            try {
                new_room = new GameRoom(notification.roomId, game_udp_socket,
                    notification.player1Ip.value(), notification.player1UdpPort,
                    notification.player2Ip.value(), notification.player2UdpPort);
            }
            catch (const std::exception& e) {
                std::cerr << "[DedicatedServer] Excepción creando GameRoom: " << e.what() << std::endl;
                // Notificar error al matchmaking service?
                delete matchmaking_service_client; // Limpiar socket del cliente
                return;
            }


            { // Alcance para los mutex
                std::lock_guard<std::mutex> lock(rooms_mutex);
                active_game_rooms.push_back(new_room);
                // Lanzar el thread para la nueva sala
                game_room_threads.emplace_back(&GameRoom::run, new_room);
            }

            { // Registrar jugadores en el mapa para demultiplexar UDP
                std::lock_guard<std::mutex> map_lock(client_map_mutex);
                client_to_room_map[{notification.player1Ip.value(), notification.player1UdpPort}] = new_room;
                client_to_room_map[{notification.player2Ip.value(), notification.player2UdpPort}] = new_room;
            }
            std::cout << "[DedicatedServer] GameRoom " << new_room->getRoomId() << " iniciada en un nuevo thread." << std::endl;

        }
        else {
            std::cerr << "[DedicatedServer] Error al deserializar notificación de nueva partida o tipo incorrecto." << std::endl;
        }
    }
    else {
        std::cerr << "[DedicatedServer] Error recibiendo notificación del MatchmakingService." << std::endl;
    }

    // El MatchmakingService podría cerrar la conexión después de enviar la notificación,
    // o podríamos enviarle un ACK. Por ahora, simplemente cerramos y eliminamos nuestro lado.
    matchmaking_service_client->disconnect();
    delete matchmaking_service_client;


}

void DedicatedServer::udpListenLoop()
{
    std::cout << "[DedicatedServer-UDPThread] Escuchando paquetes UDP de juego..." << std::endl;
    sf::Packet received_packet;
   
    std::optional<sf::IpAddress> sender_ip;
    unsigned short sender_port = 0;

    while (server_running_flag) {
        // game_udp_socket está en modo no bloqueante
        if (game_udp_socket.receive(received_packet, sender_ip, sender_port) == sf::Socket::Status::Done) {
            // std::cout << "UDP Packet from " << sender_ip.toString() << ":" << sender_port << std::endl;
            GameRoom* target_room = nullptr;
            {
                std::lock_guard<std::mutex> lock(client_map_mutex);
                auto it = client_to_room_map.find({ sender_ip.value(), sender_port});
                if (it != client_to_room_map.end()) {
                    target_room = it->second;
                }
            }

            if (target_room && target_room->isRunning()) {
                target_room->processUdpPacket(sender_ip.value(), sender_port, received_packet);
            }
            else {
                if (!target_room) {
                    //std::cerr << "[DedicatedServer-UDPThread] Paquete UDP de un cliente no asociado a ninguna sala: "
                    //          << sender_ip.toString() << ":" << sender_port << std::endl;
                }
                // Si target_room existe pero no está isRunning(), simplemente ignoramos el paquete.
            }
            received_packet.clear(); // Limpiar para la siguiente recepción
        }
        else {
            // Si no es sf::Socket::Done y el socket es no bloqueante, probablemente fue sf::Socket::NotReady
            // Damos un respiro para no quemar CPU
           // std::this_thread::sleep_for(sf::milliseconds(1));
        }
    }
    std::cout << "[DedicatedServer-UDPThread] Detenido." << std::endl;
}
