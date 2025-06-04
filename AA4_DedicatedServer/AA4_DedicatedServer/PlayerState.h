#pragma once

#include <SFML/System/Vector2.hpp>


struct PlayerState {
    sf::Vector2f position = { 0.f, 0.f };
    int health = 5; 
    int lives = 3;  

    bool wantsToShoot = false;      // Si el cliente envió input de disparo
    float moveDirection = 0.0f;   // Input de movimiento
    bool jumpRequested = false;     // Input de salto
    sf::Vector2f velocity = { 0.f, 0.f }; // Velocidad actual del jugador
    bool onGround = false;                 // Si está en el suelo
};