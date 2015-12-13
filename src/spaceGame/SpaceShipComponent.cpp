//
// Created by simon on 2015-12-12.
//

#include <MoveComponent.h>
#include <InputManager.h>
#include <ResourceManager.h>
#include <Camera.h>
#include <SmokeParticle.h>
#include <ParticleGenerator.h>
#include <Utils.h>
#include "SpaceShipComponent.h"
#include "float3x3.h"


SpaceShipComponent::SpaceShipComponent(struct HudRenderer::HudConfig* hudConf, float* cameraThetaLocation, GameObject* ship,
                                       ParticleGenerator* generator1, ParticleGenerator* generator2, State* state)
        : MoveComponent(ship){
    this->hudConf = hudConf;
    this->cameraThetaLocation = cameraThetaLocation;
    maxSpeed = 0.5f;
    this->generator1 = generator1;
    this->generator2 = generator2;
    this->state = state;


}

void SpaceShipComponent::onDeath() {
        generator1->setLooping(false);
        generator2->setLooping(false);
    *state = Died;
}

void SpaceShipComponent::update(float dt) {

    checkKeyPresses(dt);
    MoveComponent::update(dt);
    hudConf->speed = length(velocity)*200;

    float3 normVector = normalize(frontDir);
    float3 left = cross(normVector, make_vector(0.0f, 1.0f, 0.0f));
    generator1->m_position = location - normVector * 4.0 + left;
    generator2->m_position = location - normVector * 4.0 - left;

};

float SpaceShipComponent::getTotalRotation(){
    return totRotation;
}

void SpaceShipComponent::checkKeyPresses(float dt) {

    InputManager* im = InputManager::getInstance();
    if (im->isKeyDown('w',false) || im->isKeyDown('s',false)) {
        if(abs(accelerationSpeed) >= 0.00007f) {
            float ratio = 0.00007f/abs(accelerationSpeed);
            accelerationSpeed *= ratio;
        } else
            accelerationSpeed += 0.0000005f;
        acceleration = normalize(frontDir)*accelerationSpeed * (im->isKeyDown('w',false) ? 1 : -1);
    }else{
        acceleration = make_vector(0.0f,0.0f,0.0f);
    }

    if(im->isKeyDown('p',false)){
        velocity.y = 0.05f;
    }else if(im->isKeyDown('l',false)){
        velocity.y = -0.05f;
    }else
        velocity.y = 0.0f;

    float speedDif = turnSpeed*(im->isKeyDown('a',false) ? 1 : -1);
    if (im->isKeyDown('a',false) || im->isKeyDown('d',false)) {
        rotationSpeed.y = speedDif;
        totRotation += speedDif*dt;
        *cameraThetaLocation += speedDif*dt;
        frontDir = make_rotation_y<float3x3>(speedDif*dt) * frontDir;
    }else
        rotationSpeed.y = 0;
}

float3 SpaceShipComponent::getFrontDir() {
    return frontDir;
}


