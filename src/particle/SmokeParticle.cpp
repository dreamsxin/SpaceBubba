#include <Utils.h>
#include "SmokeParticle.h"


float3 SmokeParticle::initialPosition() {
    float rand = getRand(0.0f, 360.0f);
    float rand2  = getRand(0.0f, 1.0f);
    return make_vector((float)cos(rand) * rand2,
                       (float)sin(rand) * rand2,
                       (float)sin(rand) * rand2);
}

float3 SmokeParticle::initialVelocity() {
    return make_vector(getRand(-.1f, .1f), getRand(-.1f, .1f), getRand(-.1f, .1f));
}

float3 SmokeParticle::accelerate(float3 velocity) {
    return make_vector(velocity.x, velocity.y, velocity.z);
}

float SmokeParticle::calcLifetime() {
    return getRand(0.0f, 60000.0f);
}

float3 SmokeParticle::calcParticleScale() {
    return make_vector(.7f, .7f, .7f);
}

bool SmokeParticle::loop(float dt) {
    return false;
};
