#pragma once

#include <SFML/System/Vector2.hpp>

struct PlayerState {
    sf::Vector2f position = { 0.f, 0.f };
    int health = 5;
    int lives = 3;

    bool wantsToShoot = false;
    float moveDirection = 0.0f;
    bool jumpRequested = false; // <--- NUEVO: Input de salto del cliente
    sf::Vector2f velocity = { 0.f, 0.f }; // <--- NUEVO: Velocidad del jugador
    bool onGround = false;                 // <--- NUEVO: Estado de si está en el suelo
};