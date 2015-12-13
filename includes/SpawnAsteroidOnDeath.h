//
// Created by simon on 2015-12-13.
//

#ifndef BUBBA_3D_SPAWNONDEATH_H
#define BUBBA_3D_SPAWNONDEATH_H

#include <BFBroadPhase.h>
#include <Scene.h>

class SpawnAsteroidOnDeath : public IComponent{

public:
    SpawnAsteroidOnDeath(GameObject* object, Scene *scene, BFBroadPhase *collisionHandler, float3 scale);
    void onDeath() ;
    void update(float dt) {};

private:
    GameObject* gameObject;
    Scene* scene;
    BFBroadPhase* collisionHandler;
    float3 scale;

};


#endif //BUBBA_3D_SPAWNONDEATH_H
