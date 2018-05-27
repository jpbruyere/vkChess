/*
* Vulkan Example - Physical based rendering a glTF 2.0 model (metal/roughness workflow) with image based lighting
*
* Note: Requires the separate asset pack (see data/README.md)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
* Copyright (C) 2018 by jp_bruyere@hotmail.com (instanced rendering with texture array and material ubo)
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

// PBR reference: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// glTF format: https://github.com/KhronosGroup/glTF
// tinyglTF loader: https://github.com/syoyo/tinygltf

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <vector>
#include <chrono>
#include <list>
#include <iostream>
#include <errno.h>

#include "vkpbrrenderer.h"
#include "vkrenderer.h"

#include <glm/gtx/spline.hpp>

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/Gimpact/btGImpactCollisionAlgorithm.h>

#define GLM_DEPTH_CLIP_SPACE = GLM_DEPTH_NEGATIVE_ONE_TO_ONE
//GLM_DEPTH_ZERO_TO_ONE

//target id's stored in userIndex of body instance
#define BUMP_BDY_ID 1
#define BALL_BDY_ID 2
#define DAMP_BDY_ID 3
#define DOOR_BDI_ID 5
#define SPIN_BDI_ID 6
//target id range from TARG_BDY_ID + targetGroups.size, target index is in userIndex2 of body instance
#define TARG_BDY_ID 50

#define BT_DEBUG_DRAW 0

constexpr unsigned int str2int(const char* str, int h = 0)
{
    return !str[h] ? 5381 : (str2int(str, h+1) * 33) ^ str[h];
}
double diffclock( clock_t clock1, clock_t clock2 ) {

    double diffticks = clock1 - clock2;
    double diffms    = diffticks / ( CLOCKS_PER_SEC  );

    return diffms;
}
std::map<const btCollisionObject*,std::vector<btManifoldPoint*>> objectsCollisions;

void myTickCallback(btDynamicsWorld *dynamicsWorld, btScalar timeStep) {
   objectsCollisions.clear();
   int numManifolds = dynamicsWorld->getDispatcher()->getNumManifolds();
   for (int i = 0; i < numManifolds; i++) {
       btPersistentManifold *contactManifold = dynamicsWorld->getDispatcher()->getManifoldByIndexInternal(i);
       const btCollisionObject *objA = contactManifold->getBody0();
       if (objA->getUserIndex()==0)
           continue;
       const btCollisionObject *objB = contactManifold->getBody1();
       if (objB->getUserIndex()==0)
           continue;
       std::vector<btManifoldPoint*>& collisionsA = objectsCollisions[objA];
       std::vector<btManifoldPoint*>& collisionsB = objectsCollisions[objB];
       int numContacts = contactManifold->getNumContacts();
       for (int j = 0; j < numContacts; j++) {
           btManifoldPoint& pt = contactManifold->getContactPoint(j);
           collisionsA.push_back(&pt);
           collisionsB.push_back(&pt);
       }
   }
}

/*void customNearCallback(btBroadphasePair& collisionPair, btCollisionDispatcher& dispatcher, const btDispatcherInfo& dispatchInfo)
{
        btCollisionObject* colObj0 = (btCollisionObject*)collisionPair.m_pProxy0->m_clientObject;
        btCollisionObject* colObj1 = (btCollisionObject*)collisionPair.m_pProxy1->m_clientObject;

        if (dispatcher.needsCollision(colObj0,colObj1))
        {
            //dispatcher will keep algorithms persistent in the collision pair
            if (!collisionPair.m_algorithm)

                collisionPair.m_algorithm = dispatcher.findAlgorithm (colObj0,colObj1, dispatcher.getManifoldByIndexInternal(0), BT_CONTACT_POINT_ALGORITHMS);
            }

            if (collisionPair.m_algorithm)
            {
                btManifoldResult contactPointResult(colObj0,colObj1);

                if (dispatchInfo.m_dispatchFunc == 		btDispatcherInfo::DISPATCH_DISCRETE)
                {
                    //discrete collision detection query
                    collisionPair.m_algorithm->processCollision(colObj0,colObj1,dispatchInfo,&contactPointResult);
                } else
                {
                    //continuous collision detection query, time of impact (toi)
                    float toi = collisionPair.m_algorithm->calculateTimeOfImpact(colObj0,colObj1,dispatchInfo,&contactPointResult);
                    if (dispatchInfo.m_timeOfImpact > toi)
                        dispatchInfo.m_timeOfImpact = toi;

                }
            }
        }

}*/
void customNearCallback(btBroadphasePair& collisionPair,
  btCollisionDispatcher& dispatcher, const btDispatcherInfo& dispatchInfo) {

    // Do your collision logic here
    btCollisionObject* colObj0 = (btCollisionObject*)collisionPair.m_pProxy0->m_clientObject;
    btCollisionObject* colObj1 = (btCollisionObject*)collisionPair.m_pProxy1->m_clientObject;

    if (dispatcher.needsCollision(colObj0,colObj1))
    {
        btCollisionObjectWrapper obj0Wrap(0,colObj0->getCollisionShape(),colObj0,colObj0->getWorldTransform(),-1,-1);
        btCollisionObjectWrapper obj1Wrap(0,colObj1->getCollisionShape(),colObj1,colObj1->getWorldTransform(),-1,-1);


        //dispatcher will keep algorithms persistent in the collision pair
        if (!collisionPair.m_algorithm)
        {
            collisionPair.m_algorithm = dispatcher.findAlgorithm(&obj0Wrap,&obj1Wrap,0, BT_CONTACT_POINT_ALGORITHMS);
        }

        if (collisionPair.m_algorithm)
        {
            btManifoldResult contactPointResult(&obj0Wrap,&obj1Wrap);

            if (dispatchInfo.m_dispatchFunc == 		btDispatcherInfo::DISPATCH_DISCRETE)
            {
                //discrete collision detection query

                collisionPair.m_algorithm->processCollision(&obj0Wrap,&obj1Wrap,dispatchInfo,&contactPointResult);
            } else
            {
                //continuous collision detection query, time of impact (toi)
                btScalar toi = collisionPair.m_algorithm->calculateTimeOfImpact(colObj0,colObj1,dispatchInfo,&contactPointResult);
                if (dispatchInfo.m_timeOfImpact > toi)
                    dispatchInfo.m_timeOfImpact = toi;

            }
        }
    }
    // Only dispatch the Bullet collision information if you want the physics to continue
    //dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);
}
void check_collisions (btDynamicsWorld *dynamicsWorld, void* vkapp);

float flipperStrength = 0.0005f;
float damperStrength = 0.04f;
float bumperStrength = 0.02f;

const btTransform slopeRotMatrix = btTransform(btQuaternion(btVector3(1,0,0), 7.f * M_PI / 180.0), btVector3(0,0,0));


glm::mat4 btTransformToGlmMat (const btTransform &trans){
    btVector3 o = trans.getOrigin();

    btQuaternion btQ = trans.getRotation();
    glm::quat q = glm::quat (btQ.getW(), btQ.getX(), btQ.getY(), btQ.getZ());
    return glm::translate(glm::mat4(1.0),glm::vec3(o.getX(), o.getY(), o.getZ())) * glm::mat4(q);
}

struct Target {
    vkglTF::Model* modGrp;
    uint32_t    instanceIdx;
    //vks::ModelGroup::InstanceData* pInstance;
    uint32_t    materialIdx;
    btScalar    xOffset;
    bool        state;      //true when reached
    uint        points;     //points when hit
    btRigidBody* body;

    Target (uint _points = 10) {
        points  = _points;
        state   = false;
        xOffset = 0.0;
        materialIdx = 0;
        body = NULL;
    }
};
/*
class TargetGroup
{

public:
    std::vector<Target> targets;
    float       spacing;
    float       zAngle;
    btVector3   position;
    float       totalWidth = 0;
    uint32_t    targetCount = 0;
    clock_t     reachedTime;        //store when all targets was hit
    float       resetDelay = 1;     //reset delay in seconds
    int         reachedTarget = 0;  //number of reached target

    void tryReset (clock_t curTime) {
        if (diffclock(curTime, reachedTime) < resetDelay)
            return;
        for(std::vector<Target>::iterator it = targets.begin(); it != targets.end(); ++it) {
            it->modGrp->instanceDatas[it->instanceIdx].modelMat = btTransformToGlmMat(it->body->getWorldTransform());
            it->body->setActivationState(ACTIVE_TAG);
            it->state = false;
        }
        reachedTarget = 0;
    }
    btTransform getTransformation (int i) {
        return slopeRotMatrix *
                btTransform(btQuaternion(0, 0, 0, 1), btVector3(position.getX() , position.getY(), position.getZ())) *
                btTransform(btQuaternion(btVector3(0,1,0), zAngle), btVector3(0,0,0)) *
                btTransform(btQuaternion(0, 0, 0, 1), btVector3(targets[i].xOffset,0,0));
    }
    void addTarget (vkglTF::Model* pModGrp, uint32_t modelIdx, uint32_t modelPartIdx, uint32_t _materialIndex, uint _points = 10) {
        //glm::vec3 newPos = glm::vec3(0,0,0);
        Target target(_points);

        float addedW = pModGrp->models[modelIdx].partDims[modelPartIdx].size.x;
        if (targetCount > 0) {
            float halfAW = (addedW + spacing) / 2.f;
            for (int i=0; i<targetCount; i++)
                targets[i].xOffset -= halfAW;

            target.xOffset = (totalWidth + spacing) / 2.f ;
            totalWidth += addedW + spacing;
        }else
            totalWidth = addedW;

        //target.pInstance = &pModGrp->instanceDatas [pModGrp->addInstance (modelIdx, modelPartIdx, glm::mat4(), _materialIndex)];
        target.modGrp = pModGrp;
        target.instanceIdx = pModGrp->addInstance (modelIdx, modelPartIdx, glm::mat4(), _materialIndex);
        targets.push_back (target);

        targetCount++;
    }

    TargetGroup(btVector3 _position, float _zAngle = 0.f, float _spacing = 0.01) {
        position = _position;
        zAngle = _zAngle;
        spacing = _spacing;
    }
};*/

class VulkanExample : public vkPbrRenderer
{
    struct MovingObject {
        btRigidBody* body;
        uint32_t instanceIdx;
    };

    std::vector<MovingObject> balls;

    float ballSize  = 0.027;
    float sloap     = 7.f * M_PI/180.0f;

    btVector3 upVector = btVector3(0, cos(sloap), sin(sloap));
    btVector3 pushDir = btVector3(0,0,-1);

    float   iterations          = 10;
    float   timeStep            = 1.0f / 1000.0;
    float   fixedTimeStepDiv    = 1.0f / 600.0;
    float   maxSubsteps         = 10.f;
    int     subSteps            = 0;
    clock_t lastTime;


    bool    splitImpulse        = false;
    const float inpulse     = 2.f;
    const int   target      = 2;
    bool        leftFlip    = false;
    bool        rightFlip   = false;


    btBroadphaseInterface*              broadphase = nullptr;
    btDefaultCollisionConfiguration*    collisionConfiguration = nullptr;
    btCollisionDispatcher*              dispatcher = nullptr;
    btSequentialImpulseConstraintSolver* solver = nullptr;
    btDiscreteDynamicsWorld*            dynamicsWorld = nullptr;

#if BT_DEBUG_DRAW
    vkRenderer* debugRenderer = nullptr;
#endif

public:


    VulkanExample() : vkPbrRenderer()
    {
        title = "Vulkan glTf 2.0 PBR";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 1.0f;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 250.0f);
        camera.rotationSpeed = 0.05f;

        camera.setRotation({ 32.0f, 0.0f, 0.0f });
        camera.setPosition({ .05f, -0.3f, -0.64f });
    }

    ~VulkanExample()
    {
        delete dynamicsWorld;
        delete solver;
        delete dispatcher;
        delete collisionConfiguration;
        delete broadphase;
    }

    virtual void loadAssets() {
        vkPbrRenderer::loadAssets();

        models.object.loadFromFile("./../data/models/pinball.gltf", vulkanDevice, queue, true);

        /*models.object.addInstance("Plane.023", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("ramp-left", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("ramp-right", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("left_damper", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("right_damper", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));

        models.object.addInstance("bumper", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));*/

        init_physics();
    }
    virtual void prepare() {
        vkPbrRenderer::prepare();

#if BT_DEBUG_DRAW
        debugRenderer = new vkRenderer (vulkanDevice, &swapChain, depthFormat, settings.sampleCount,
                                                        frameBuffers, &uniformBuffers.matrices);
#endif
    }


    btRigidBody* addRigidBody (btCollisionShape* shape, int group = 0, int mask = 0xffff, btScalar restitution = 0., btScalar friction = 0., btScalar mass = 0, btTransform transformation = btTransform()){
        btRigidBody* body = nullptr;
        btMotionState* motionState = nullptr;
        btVector3 fallInertia(0, 0, 0);

        if (mass > 0.f)   {
            shape->calculateLocalInertia(mass, fallInertia);
            motionState = new btDefaultMotionState(transformation);
        }

        btRigidBody::btRigidBodyConstructionInfo rbci(mass, motionState, shape, fallInertia);
        rbci.m_restitution = restitution;
        rbci.m_friction = friction;

        body = new btRigidBody(rbci);
        dynamicsWorld->addRigidBody(body, group, mask);

        return body;
    }
    void initPhysicalBodies () {
        //vks::ModelGroup* modBodies = new vks::ModelGroup(vulkanDevice, queue);

        btCollisionShape* shape = nullptr;
        btRigidBody* body = nullptr;
        btRigidBody::btRigidBodyConstructionInfo rbci(0, nullptr, shape, btVector3(0, 0, 0));
        btVector3 vUp = btVector3(0,1,0);
        btScalar mass = 0.6;
        btVector3 fallInertia(0, 0, 0);
        btHingeConstraint* hinge;
        btTransform trBody;

        int modBodiesIdx = -1;

        //low plane
        shape = new btStaticPlaneShape(upVector, 0);
        shape->setMargin(0.001);
        addRigidBody(shape, 0x02,0x01, 0.1, 0.5);
/*
        //static bodies
        modBodiesIdx = modBodies->addModel(getAssetPath() + "models/pinball-static.obj");
        for (int i = 0; i<modBodies->models[modBodiesIdx].parts.size() ; i++)
            addRigidBody (modBodies->getConvexHullShape(modBodiesIdx, i), 0x02,0x01, 0.8, 0.2);

        //tests
        modBodiesIdx = modBodies->addModel(getAssetPath() + "models/pinball.obj");
        for (int i = 0; i<modBodies->models[modBodiesIdx].parts.size() ; i++)
            addRigidBody (modBodies->getConvexHullShape(modBodiesIdx, i), 0x02,0x01, 0.8, 0.2);



        //damper
        modBodiesIdx = modBodies->addModel(getAssetPath() + "models/pinball-damp.obj");
        for (int i = 0; i<modBodies->models[modBodiesIdx].parts.size() ; i++)
            addRigidBody (modBodies->getConvexHullShape(modBodiesIdx, i), 0x02,0x01, 0., 0.1)
                    ->setUserIndex(DAMP_BDY_ID);



        modBodiesIdx = modBodies->addModel(getAssetPath() + "models/pinball-lp.obj");
        for (int i = 0; i < modBodies->models[modBodiesIdx].parts.size(); i++){
            switch (str2int(modBodies->models[modBodiesIdx].parts[i].name.c_str())) {
            case str2int("target1-lp_Cube.016")://target1
                for(int j=0; j<targetGroups.size(); j++) {
                    shape = modBodies->getConvexHullShape(modBodiesIdx, i);
                    TargetGroup* tg = targetGroups[j];
                    for(int k=0; k<targetGroups[j]->targetCount; k++) {
                        btTransform t = targetGroups[j]->getTransformation(k);
                        body = addRigidBody (shape, 0x02,0x01, 0.5, 0.1);
                        body->setWorldTransform (t);
                        body->setUserIndex(TARG_BDY_ID + j);
                        body->setUserIndex2(k);
                        tg->targets[k].body = body;
                        //targetGroups[j]->targets[k].pInstance->modelMat = btTransformToGlmMat (t);
                        tg->targets[k].modGrp->instanceDatas[tg->targets[k].instanceIdx].modelMat = btTransformToGlmMat (t);
                    }
                }
                break;
            case str2int("flip-lp_Circle.117")://flippers
                mass = 0.09;
                shape = modBodies->getConvexHullShape(modBodiesIdx, i);
                shape->calculateLocalInertia(mass, fallInertia);
                //right
                body = addRigidBody (shape, 0x02,0x01, 0.1, 0.2, mass,
                            slopeRotMatrix * btTransform(btQuaternion(btVector3(0,0,1), M_PI), btVector3(0.09194,0.01479,0.39887)));
                worldObjs[worldObjFlip].body = body;
                hinge = new btHingeConstraint(*body, btVector3(0,0,0), vUp, false);
                hinge->setLimit (M_PI - 0.6, M_PI + 0.5, 0.001f, 1.0f, -0.001f);
                dynamicsWorld->addConstraint(hinge);
                //left
                body = addRigidBody (shape, 0x02,0x01, 0.1, 0.2, mass,
                            slopeRotMatrix *
                            btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.09194,0.01479,0.39887)));
                worldObjs[worldObjFlip+1].body = body;
                hinge = new btHingeConstraint (*body, btVector3(0,0,0), vUp, false);
                hinge->setLimit (-0.5, 0.6, .001f,1.0f,-0.001f);
                dynamicsWorld->addConstraint(hinge);
                //top left
                shape = modBodies->getConvexHullShape(modBodiesIdx, i, 0.92f);
                shape->calculateLocalInertia(mass, fallInertia);
                body = addRigidBody (shape, 0x02,0x01, 0.1, 0.2, mass,
                            slopeRotMatrix *
                            btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.22988,0.01479,-0.07538)));
                worldObjs[worldObjFlip+2].body = body;
                hinge = new btHingeConstraint (*body, btVector3(0,0,0), vUp, false);
                hinge->setLimit (-1.20, 0., .001f,1.0f,-0.001f);
                dynamicsWorld->addConstraint(hinge);
                break;
            case str2int("door1-lp_door1")://doors
                shape = modBodies->getConvexHullShape(modBodiesIdx, i);
                mass = 0.003;
                shape->calculateLocalInertia(mass, fallInertia);
                createDoor (shape, mass, btVector3(0.28105,0,-0.27905), 0.148, 1., -0.0001);
                createDoor (shape, mass, btVector3(-0.06332,0,-0.5206), M_PI_2+0.1, M_PI_2 + 0.6, -0.0001);
                createDoor (shape, mass, btVector3(0.05674,0,-0.5206), M_PI_2-0.38, M_PI_2 + 0.6, -0.0001);
                break;
            case str2int("guide-lp_guide")://guide
                shape = modBodies->getConvexHullShape(modBodiesIdx, i);
                createGuide(shape, btVector3(-0.188,0,0.248),0);
                createGuide(shape, btVector3(0.188,0,0.248),0);
                break;
            case str2int("spinner-lp_Cube.011")://spinner
                shape = modBodies->getConvexHullShape(modBodiesIdx, i);
                mass = 0.01;
                shape->calculateLocalInertia(mass, fallInertia);
                trBody = slopeRotMatrix *
                        btTransform(btQuaternion(btVector3(0,1,0), -21.f * M_PI / 180.0), btVector3(0.24554,0.02731,-0.13162));
                body = addRigidBody (shape, 0x02,0x01, 0., 0.01, mass, trBody);
                body->setUserIndex(SPIN_BDI_ID);
                body->setUserIndex2(1);
                worldObjs[worldObjSpinner].body = body;

                hinge = new btHingeConstraint (*body, btVector3(0,0.006,0), btVector3(1,0,0), false);
                hinge->setLimit (0., 2 * M_PI, 0.1f, 0.f,-1.0f);
                dynamicsWorld->addConstraint(hinge);
                //move spinner frame which is static, not move by physic update
                modGrp->instanceDatas [instSpinnerFrameIdx].modelMat = btTransformToGlmMat (trBody);
                break;
            case str2int("bump-lp_Circle.017")://bumper
                //bumpers
                btVector3 bumperPos[] = {
                    btVector3(-0.099, 0.0,-0.307),
                    btVector3(-0.017, 0.0,-0.239),
                    btVector3(0.058, 0.0,-0.307),
                };
                shape = modBodies->getConvexHullShape(modBodiesIdx, i);
                for (int j = 0; j<3 ; j++) {
                    trBody = slopeRotMatrix * btTransform(btQuaternion(0, 0, 0, 1), bumperPos[j]);
                    body = addRigidBody (shape, 0x02,0x01, 1.2, 0.0, 0, trBody);
                    body->setUserIndex(BUMP_BDY_ID);
                    body->setWorldTransform (trBody);
                    modGrp->instanceDatas [instBumperIdx+j].modelMat = btTransformToGlmMat (trBody);
                }
                break;
            }
        }
*/
        //balls
        shape = new btSphereShape(ballSize * 0.5);
        mass = 0.08;
        shape->calculateLocalInertia(mass, fallInertia);

        for (int i=0; i < 1; i++) {
            //body = addRigidBody (shape, 0x01,0xff, 0.5, 0.01, mass, btTransform(btQuaternion(0, 0, 0, 1), btVector3(0.298,0.05,0.2)));
            body = addRigidBody (shape, 0x01,0xff, 0.5, 0.01, mass, btTransform(btQuaternion(0, 0, 0, 1), btVector3(0,0,0)));
            body->setUserIndex             (BALL_BDY_ID);
            body->setRollingFriction       (.00001);
            body->setSpinningFriction      (0.1);
            body->setAnisotropicFriction   (shape->getAnisotropicRollingFrictionDirection(),
                                                         btCollisionObject::CF_ANISOTROPIC_ROLLING_FRICTION);
            body->setCcdMotionThreshold    (ballSize * 0.5f);
            body->setCcdSweptSphereRadius  (ballSize * 0.1f);

            balls.push_back({body,
                models.object.addInstance("ball", glm::mat4(1.0))});
        }

        /*modBodies->destroy();
        delete(modBodies);*/
    }

    void init_physics() {
        broadphase = new btDbvtBroadphase();

        collisionConfiguration = new btDefaultCollisionConfiguration();

        dispatcher = new btCollisionDispatcher(collisionConfiguration);
        //dispatcher->setNearCallback(customNearCallback);

        btGImpactCollisionAlgorithm::registerAlgorithm(dispatcher);

        solver = new btSequentialImpulseConstraintSolver;

        dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
        dynamicsWorld->setGravity(btVector3(0, -10, 0));

        dynamicsWorld->getSolverInfo().m_erp2 = 0.f;
        dynamicsWorld->getSolverInfo().m_globalCfm = 0.f;
        //dynamicsWorld->getDispatchInfo().m_dispatchFunc = btDispatcherInfo::DISPATCH_CONTINUOUS;

        dynamicsWorld->setInternalTickCallback(myTickCallback);

        btContactSolverInfo& info = dynamicsWorld->getSolverInfo();
        info.m_numIterations = int(iterations);
        info.m_splitImpulse = int(splitImpulse);

        initPhysicalBodies();

    }

    void update_physics () {

        btTransform trans;
        /*for (int i=0; i<doors.size(); i++) {
            doors[i].body->getMotionState()->getWorldTransform(trans);
            modGrp->instanceDatas[doors[i].instanceIdx].modelMat = btTransformToGlmMat(trans);
        }*/
        for (int i=0; i < balls.size(); i++) {
            balls[i].body->getMotionState()->getWorldTransform(trans);
            models.object.instanceDatas[balls[i].instanceIdx].modelMat = btTransformToGlmMat(trans);
        }

        /*//scale top left flipper
        modGrp->instanceDatas[worldObjs[worldObjFlip+2].instanceIdx].modelMat =
                modGrp->instanceDatas[worldObjs[worldObjFlip+2].instanceIdx].modelMat *
                glm::scale(glm::mat4(), glm::vec3(0.92,0.92,0.92));*/

        models.object.updateInstancesBuffer();
    }


    void step_physics () {
        /*
        for (int i=0; i<doors.size(); i++)
            doors[i].body->applyTorqueImpulse(btVector3(0,1,0)* doors[i].strength);

        btVector3 avSpinner = worldObjs[worldObjSpinner].body->getAngularVelocity();
        if (avSpinner.getX() != 0) {

            avSpinner.setX(avSpinner.getX()*0.999);
            worldObjs[worldObjSpinner].body->setAngularVelocity (avSpinner);
        }

        if (leftFlip) {
            worldObjs[worldObjFlip+1].body->applyTorqueImpulse(btVector3(0,1,0)* flipperStrength);
            worldObjs[worldObjFlip+2].body->applyTorqueImpulse(btVector3(0,1,0)* flipperStrength);
        }else {
            worldObjs[worldObjFlip+1].body->applyTorqueImpulse(btVector3(0,-1,0)* flipperStrength);
            worldObjs[worldObjFlip+2].body->applyTorqueImpulse(btVector3(0,-1,0)* flipperStrength);
        }

        if (rightFlip) {
            worldObjs[worldObjFlip].body->applyTorqueImpulse(btVector3(0,1,0)* -flipperStrength);
        }else {
            worldObjs[worldObjFlip].body->applyTorqueImpulse(btVector3(0,1,0)* flipperStrength);
        }
*/
        clock_t time = clock();

        float diff = float(diffclock(time, lastTime));

        if (diff > timeStep) {
            lastTime = time;
            dynamicsWorld->updateAabbs();
            subSteps = dynamicsWorld->stepSimulation(diff,int(maxSubsteps),fixedTimeStepDiv);
            update_physics();
            //check_collisions(dynamicsWorld, this);

            /*
            for(int j=0; j<targetGroups.size(); j++) {
                TargetGroup* tg = targetGroups[j];
                if (tg->reachedTarget == tg->targetCount)
                    tg->tryReset (time);
            }*/
        }

        //dynamicsWorld->stepSimulation(1.0/1600.0,10,1.0/1000.0);


    }

    void render () {
        if (!prepared)
            return;
        step_physics();

        prepareFrame();

        this->submit(queue, &presentCompleteSemaphore, 1);
#if BT_DEBUG_DRAW
        debugRenderer->submit(queue,&this->drawComplete, 1);
        VK_CHECK_RESULT(swapChain.queuePresent(queue, debugRenderer->drawComplete));
#else
        VK_CHECK_RESULT(swapChain.queuePresent(queue, this->drawComplete));
#endif
        //debugRenderer->submit(queue,&this->presentCompleteSemaphore, 1);
        //
        //VK_CHECK_RESULT(swapChain.queuePresent(queue, debugRenderer->drawComplete));

    }

};

VulkanExample *vulkanExample;

// OS specific macros for the example main entry points
#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (vulkanExample != NULL)
    {
        vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
    }
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    for (int32_t i = 0; i < __argc; i++) { VulkanExample::args.push_back(__argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow(hInstance, WndProc);
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point
// A note on app_dummy(): This is required as the compiler may otherwise remove the main entry point of the application
void android_main(android_app* state)
{
    app_dummy();
    vulkanExample = new VulkanExample();
    state->userData = vulkanExample;
    state->onAppCmd = VulkanExample::handleAppCommand;
    state->onInputEvent = VulkanExample::handleAppInput;
    androidApp = state;
    vks::android::getDeviceConfig();
    vulkanExample->renderLoop();
    delete(vulkanExample);
}
#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi
static void handleEvent()
{
}
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
static void handleEvent(const xcb_generic_event_t *event)
{
    if (vulkanExample != NULL)
    {
        vulkanExample->handleEvent(event);
    }
}
int main(const int argc, const char *argv[])
{
    for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
    vulkanExample = new VulkanExample();
    vulkanExample->initVulkan();
    vulkanExample->setupWindow();
    vulkanExample->prepare();
    vulkanExample->renderLoop();
    delete(vulkanExample);
    return 0;
}
#endif
