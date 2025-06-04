#include "DedicatedServer.h"
#include <iostream>
#include <csignal> // Para signal y SIGINT
#include <atomic>



std::atomic<bool> keep_running(true);

void sigint_handler(int signal) {
    std::cout << "\n[MainDedicated] SIGINT recibido, deteniendo servidor..." << std::endl;
    keep_running = false;
}



int main() {

    signal(SIGINT, sigint_handler); // Capturar Ctrl+C

    unsigned short game_port = 56000;    // Puerto UDP para el juego
    unsigned short admin_port = 56001;   // Puerto TCP para administración
    size_t pool_threads = std::thread::hardware_concurrency(); 


    std::string map_file_path = "Data/map.txt"; 

    DedicatedServer dedicated_server(game_port, admin_port, pool_threads, map_file_path); 

    //Lanza dedicated_server.run() en un nuevo std::thread (server_thread).
    std::thread server_thread(&DedicatedServer::run, &dedicated_server);
    std::cout << "[MainDedicated] Servidor Dedicado iniciado. Presiona Ctrl+C para detener." << std::endl;

    //Entra en un bucle while (keep_running) que espera a que keep_running se ponga a false (por Ctrl+C).
    while (keep_running) {
      
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "[MainDedicated] Señal de detención recibida. Solicitando parada del servidor..." << std::endl;
    dedicated_server.stop(); // Llama a stop

    if (server_thread.joinable()) {
        std::cout << "[MainDedicated] Esperando que el thread principal del servidor finalice..." << std::endl;
        server_thread.join();
    }
    std::cout << "[MainDedicated] Servidor Dedicado detenido limpiamente." << std::endl;

    return 0;

} 
    




