#pragma once

#include <SFML/Network.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "GameRoom.h"
#include "ThreadPool.h" // Incluir nuestro nuevo ThreadPool
#include <optional> 




// Struct para la info que viene del MatchmakingService
struct NewGameNotification {
    std::string roomId;
    std::optional<sf::IpAddress> player1Ip; // Inicializar
    unsigned short player1UdpPort = 0;            // Inicializar
    std::optional<sf::IpAddress> player2Ip; // Inicializar
    unsigned short player2UdpPort = 0;            // Inicializar

    
};

// Para el clientToRoomMap
struct ClientUDPAddress {
    sf::IpAddress ip;
    unsigned short port;

    bool operator==(const ClientUDPAddress& other) const {
        return ip == other.ip && port == other.port;
    }
};

//Revisar
struct ClientUDPAddressHash {
    std::size_t operator()(const ClientUDPAddress& addr) const {
        // Combina los hashes de la IP (convertida a string) y el puerto
        return std::hash<std::string>()(addr.ip.toString()) ^ (std::hash<unsigned short>()(addr.port) << 1);
    }
};

class DedicatedServer
{

public:

    DedicatedServer(unsigned short gamePort, unsigned short adminPort, size_t num_pool_threads, const std::string& map_file_path);  
    ~DedicatedServer();

    void run();
    void stop();


private:
    std::string m_map_file_path;

    ThreadPool thread_pool_;

    // Para notificaciones del MatchmakingService (TCP)

    sf::TcpListener admin_listener;
    std::thread admin_thread;

    std::thread admin_connection_handler_thread_;

    void adminServiceLoop();

    void handleActiveMatchmakingConnection(sf::TcpSocket matchmaking_socket);
    void handleNewGameRequest(sf::TcpSocket* matchmaking_service_client);

    //Gameplay

    sf::UdpSocket game_udp_socket; // Socket UDP principal
    std::thread udp_listen_thread;
    void udpListenLoop();

    unsigned short game_udp_port_val;
    unsigned short admin_tcp_port_val;

    std::atomic<bool> server_running_flag;

    // Gestión de salas y sus threads
  std::vector<GameRoom*> active_game_rooms;
    //std::vector<std::thread> game_room_threads; // Guardamos los threads para hacer join()
    std::mutex rooms_mutex; // Para proteger active_game_rooms y game_room_threads

    // Mapa para dirigir paquetes UDP a la GameRoom correcta
   std::unordered_map<ClientUDPAddress, GameRoom*, ClientUDPAddressHash> client_to_room_map;
    std::mutex client_map_mutex; // Para proteger client_to_room_map








};

