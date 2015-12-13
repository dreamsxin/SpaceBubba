//
// Created by johan on 2015-12-13.
//
#include <ResourceManager.h>
#include <Utils.h>
#include <StandardRenderer.h>
#include <constants.h>
#include <MoveComponent.h>
#include <TimedLife.h>
#include <SpaceShipComponent.h>
#include <Scene.h>
#include <BFBroadPhase.h>
#include "ShootComponent.h"
#include "InputManager.h"


ShootComponent::ShootComponent(GameObject* object, SpaceShipComponent *objectMover, Scene *scene, BFBroadPhase *collisionHandler, float timeToLive) {
    this->object = object;
    this->objectMover = objectMover;
    this->scene = scene;
    this->collisionHandler = collisionHandler;
    this->timeToLive = timeToLive;
}

void ShootComponent::update(float dt) {
    InputManager* im = InputManager::getInstance();
    if(im->isKeyDown((int)'r', false)) {
        spawnBullet();
    }
}


void ShootComponent::spawnBullet() {

    float4 ps = object->getModelMatrix()->c4;
    float3 location = make_vector(ps.x, ps.y, ps.z);

    Shader* standardShader = ResourceManager::getShader(SIMPLE_SHADER_NAME);
    standardShader->setUniformBufferObjectBinding(UNIFORM_BUFFER_OBJECT_MATRICES_NAME, UNIFORM_BUFFER_OBJECT_MATRICES_INDEX);

    Mesh* shotM = ResourceManager::loadAndFetchMesh("../scenes/shot.obj");
    GameObject *shot = new GameObject(shotM);
    shot->move(make_translation(location));
    shot->update(make_rotation_y<float4x4>(degreeToRad(90)));

    StandardRenderer *shotRenderer = new StandardRenderer(shotM, shot->getModelMatrix(), standardShader);
    shot->addRenderComponent(shotRenderer);
    shot->setDynamic(false);

    MoveComponent *shotMover = new MoveComponent(shot,
                                                 make_vector(0.0f, 0.0f, 0.0f),
                                                 normalize(objectMover->getFrontDir()) / 50,
                                                 make_vector(0.0f, 0.0f, 0.0f));
    TimedLife *tl = new TimedLife(shot, timeToLive);
    shot->addComponent(tl);
    shot->addComponent(shotMover);
    scene->shadowCasters.push_back(shot);
    collisionHandler->addGameObject(shot);
}
