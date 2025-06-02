#pragma once

#include <SFML/System/Vector2.hpp>


struct PlayerState {
    sf::Vector2f position = { 0.f, 0.f }; // Posición inicial
    int health = 5;
    int lives = 3;
   
    bool wantsToShoot = false; // Ejemplo de input
    float moveDirection = 0.0f; // -1 izquierda, 1 derecha, 0 quieto
};