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
#include <string>
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

#include "btvkdebugdrawer.h"

#include "bullethelpers.h"

#define CHECK_BIT(var,pos) (((var)>>(pos)) & 1)

#define GLM_DEPTH_CLIP_SPACE = GLM_DEPTH_NEGATIVE_ONE_TO_ONE
//GLM_DEPTH_ZERO_TO_ONE

//target id's stored in userIndex of body instance
#define BUMP_BDY_ID 1
#define BALL_BDY_ID 2
#define DAMP_BDY_ID 3
#define DOOR_BDI_ID 5
#define SPIN_BDI_ID 6
//target id range from TARG_BDY_ID + targetGroups.size, target index is in userIndex2 of body instance
#define TARG_LEFT_BDY_ID 50
#define TARG_RIGHT_BDY_ID 51

#define BT_DEBUG_DRAW 1

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

void myTickCallback(btDynamicsWorld *dynamicsWorld, btScalar timeStep);
void check_collisions (btDynamicsWorld *dynamicsWorld, void* vkapp);

float flipperStrength = 0.0003f;
float damperStrength = 0.02f;
float bumperStrength = 0.02f;

const btTransform slopeRotMatrix = btTransform(btQuaternion(btVector3(1,0,0), 7.f * M_PI / 180.0), btVector3(0,0,0));

class VulkanExample : public vkPbrRenderer
{
public:
    struct MovingObject {
        btRigidBody* body;
        uint32_t instanceIdx;
    };

    struct Door : MovingObject {
        btScalar strength;

        Door(btRigidBody* _body, uint32_t _instance, btScalar _strength) {
            body = _body;
            instanceIdx = _instance;
            strength = _strength;
        }
    };

    struct Target : MovingObject {
        bool state;      //true when reached
        uint points;     //points when hit

        Target (btRigidBody* _body, uint32_t _instance, uint _points = 10) {
            body = _body;
            instanceIdx = _instance;
            points  = _points;
            state   = false;
        }
    };

    struct TargetGroup
    {
        std::vector<Target> targets;
        uint32_t    id;         //used for identifying group in collision detection as userindex1
        float       spacing;
        float       zAngle;
        btVector3   position;
        clock_t     reachedTime;        //store when all targets was hit
        float       resetDelay;         //reset delay in seconds
        int         reachedTargetCount;      //number of reached target

        TargetGroup(uint32_t _id, btVector3 _position, float _zAngle = 0.f, float _spacing = 0.01) {
            id = _id;
            position = _position;
            zAngle = _zAngle;
            spacing = _spacing;
            resetDelay = 1;
            reachedTime = 0;
            reachedTargetCount = 0;
        }
        TargetGroup() {}

        void createBodies (vkglTF::Model& model) {
            float totalWidth = 0.f;

            float widths[targets.size()];

            for (int i=0; i<targets.size(); i++) {
                widths[i] = model.getPrimitiveFromInstanceIdx(targets[i].instanceIdx)->dims.size.x;
                totalWidth += widths[i] + spacing;
            }
            totalWidth -= spacing;

            float offset = -totalWidth / 2.f;

            for (int i=0; i<targets.size(); i++) {
                btTransform tr(slopeRotMatrix *
                        btTransform(btQuaternion(0, 0, 0, 1), btVector3(position.getX() , position.getY(), position.getZ())) *
                        btTransform(btQuaternion(btVector3(0,1,0), zAngle), btVector3(0,0,0)) *
                        btTransform(btQuaternion(0, 0, 0, 1), btVector3(offset + widths[i] / 2.f,0,0)));
                targets[i].body->setWorldTransform (tr);
                targets[i].body->setUserIndex(id);
                targets[i].body->setUserIndex2(i);
                model.instanceDatas[targets[i].instanceIdx].modelMat = btTransformToGlmMat(tr);
                offset += widths[i] + spacing;
            }
        }
        void checkStates (clock_t curTime, vkglTF::Model& model) {
            if (reachedTargetCount < targets.size())
                return;
            if (diffclock(curTime, reachedTime) < resetDelay)
                return;
            for (int i=0; i<targets.size(); i++) {
                model.instanceDatas[targets[i].instanceIdx].modelMat = btTransformToGlmMat(targets[i].body->getWorldTransform());
                targets[i].body->setActivationState(ACTIVE_TAG);
                targets[i].state = false;
            }
            reachedTargetCount = 0;
        }
    };

    std::vector<Door>         doors;
    std::vector<MovingObject> guides;
    std::vector<MovingObject> balls;
    std::vector<MovingObject> flippers;
    std::vector<MovingObject> bumpers;
    std::vector<MovingObject> spinners;


    TargetGroup leftTargets;
    TargetGroup rightTargets;


    float ballSize  = 0.027;
    float sloap     = 7.f * M_PI/180.0f;

    btVector3 upVector = btVector3(0, cos(sloap), sin(sloap));
    btVector3 pushDir = btVector3(0,0,-1);

    int     iterations          = 10;
    float   timeStep            = 1.0f / 10000.0;
    float   fixedTimeStepDiv    = 1.0f / 400.0;
    int     maxSubsteps         = 10.f;
    int     subSteps            = 0;
    float   lastTimeStep        = 0.f; //effective time step pass to bullet stepping func
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
    btVKDebugDrawer* debugRenderer = nullptr;
#endif

    uint player_points = 0;
    uint bumper_points = 100;
    uint damper_points = 10;
    uint sinner_points = 1;
    uint leftTarget_points = 500;

    VulkanExample() : vkPbrRenderer()
    {
        title = "Vulkan glTf 2.0 PBR";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 0.1f;
        camera.setPerspective (60.0f, (float)width / (float)height, 0.01f, 10.0f);
        camera.rotationSpeed = 0.05f;

        camera.setRotation({ 40.0f, 0.0f, 0.0f });
        camera.setPosition({ .001f, -0.35f, -0.60f });
    }

    ~VulkanExample()
    {
#if BT_DEBUG_DRAW
        debugRenderer->texSDFFont.destroy();
        delete debugRenderer;
#endif
        delete dynamicsWorld;
        delete solver;
        delete dispatcher;
        delete collisionConfiguration;
        delete broadphase;
    }

    virtual void loadAssets() {
        vkPbrRenderer::loadAssets();

        models.object.loadFromFile("./../data/models/pinball.gltf", vulkanDevice, queue, true);

        models.object.addInstance("Plane.023", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("ramp-left", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("ramp-right", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));
        models.object.addInstance("Circle.033", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));

        init_physics();
    }
    virtual void prepare() {
        vkPbrRenderer::prepare();

#if BT_DEBUG_DRAW
        vks::Texture2D fontTexture;
        fontTexture.loadFromFile ("./../data/font.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);


        debugRenderer = new btVKDebugDrawer (vulkanDevice, &swapChain, depthFormat, settings.sampleCount,
                                                        frameBuffers, &uniformBuffers.matrices,
                                             "./../data/font.fnt", fontTexture);
        debugRenderer->setDebugMode(
                    btIDebugDraw::DBG_DrawConstraintLimits
                    |btIDebugDraw::DBG_DrawFrames
                    |btIDebugDraw::DBG_DrawContactPoints
                    |btIDebugDraw::DBG_DrawConstraints
                    //|btIDebugDraw::DBG_DrawText
                    //|btIDebugDraw::DBG_DrawWireframe
                    );

        dynamicsWorld->setDebugDrawer (debugRenderer);
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

    btConvexHullShape* getConvexHullShape (vkglTF::Model& modBodies, uint32_t modelIdx, float scale = 1.0f) {
        vkglTF::Primitive* mod = &modBodies.primitives[modelIdx];
        btConvexHullShape* shape  = new btConvexHullShape();

        for (int i = 0 ; i < mod->indexCount ; i++) {
            glm::vec3 pos = modBodies.vertexBuffer [mod->vertexBase + modBodies.indexBuffer[mod->indexBase + i]].pos;
            btVector3 v = btVector3(pos.x * scale, pos.y * scale, pos.z * scale);
            shape->addPoint(v);
        }
        shape->optimizeConvexHull();
        shape->initializePolyhedralFeatures();
        shape->setMargin(0.001);

        return shape;
    }
    btConvexHullShape* getConvexHullShape (vkglTF::Model& modBodies, const std::string& name, float scale = 1.0f) {
        for (int i=0; i<modBodies.primitives.size(); i++) {
            if (name != modBodies.primitives[i].name)
                continue;
            return getConvexHullShape(modBodies, i, scale);
        }
        return nullptr;
    }
    void createDoor (btCollisionShape* shape, btScalar mass, btVector3 pos, btScalar low, btScalar high, btScalar strength) {
        static int idx = 0;

        btRigidBody* body = addRigidBody (shape, 0x02,0x01, 0., 0.01, mass,
                    slopeRotMatrix *
                    btTransform(btQuaternion(0, 0, 0, 1), pos));
        body->setUserIndex(DOOR_BDI_ID);
        body->setUserIndex2(idx);

        doors.push_back( Door (body,
            models.object.addInstance("door1", glm::mat4()), strength));

        btHingeConstraint* hinge = new btHingeConstraint (*body, btVector3(0,0,0), btVector3(0,1,0), false);
        hinge->setLimit (low, high, 0.01f,1.f,-0.001f);
        dynamicsWorld->addConstraint(hinge);

        idx++;
    }
    void createGuide (btCollisionShape* shape, btVector3 pos, btScalar angle) {
        static int idx = 0;
//        btTransform tr = slopeRotMatrix *
//                btTransform(btQuaternion(btVector3(0,1,0), angle), pos);
        btTransform tr = slopeRotMatrix *
                btTransform(btQuaternion(0, 0, 0, 1), pos);

        guides.push_back({addRigidBody (shape, 0x02,0x01, 0., 0.01),
            models.object.addInstance("guides", btTransformToGlmMat(tr))});

        guides[idx].body->setWorldTransform (tr);

        idx++;
    }

    void initPhysicalBodies () {
        vkglTF::Model modBodies;

        btCollisionShape* shape = nullptr;
        btRigidBody* body = nullptr;
        btVector3 vUp = btVector3(0,1,0);
        btScalar mass = 0.6;
        btVector3 fallInertia(0, 0, 0);
        btHingeConstraint* hinge;
        btTransform trBody;

        //low plane
        shape = new btStaticPlaneShape(upVector, 0);
        shape->setMargin(0.001);
        addRigidBody(shape, 0x02,0x01, 0.1, 0.5);

        //static bodies
        modBodies.loadFromFile("./../data/models/pinball-lp.gltf", vulkanDevice, queue, false, 1.0f, true);
        for (int i = 0; i<modBodies.primitives.size() ; i++)
            addRigidBody (getConvexHullShape (modBodies, i), 0x02,0x01, 0.8, 0.2);


        modBodies.loadFromFile("./../data/models/pinball-lp-obj.gltf", vulkanDevice, queue, false, 1.0f, true);

        addRigidBody (getConvexHullShape(modBodies, "damp-left-lp"), 0x02,0x01, 0., 0.1)
                ->setUserIndex(DAMP_BDY_ID);
        models.object.addInstance("left_damper", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));

        addRigidBody (getConvexHullShape(modBodies, "damp-right-lp"), 0x02,0x01, 0., 0.1)
                ->setUserIndex(DAMP_BDY_ID);
        models.object.addInstance("right_damper", glm::translate(glm::mat4(1.0), glm::vec3( 0,0,0)));

        for (int i = 0; i<modBodies.primitives.size() ; i++) {
            switch (str2int(modBodies.primitives[i].name.c_str())) {
            case str2int("flip-lp")://flippers
                mass = 0.09;
                shape = getConvexHullShape (modBodies, i);
                shape->calculateLocalInertia(mass, fallInertia);
                //right
                body = addRigidBody (shape, 0x02,0x01, 0.1, 0.2, mass,
                            slopeRotMatrix * btTransform(btQuaternion(btVector3(0,0,1), M_PI), btVector3(0.09194,0.01479,0.39887)));

                flippers.push_back({body,
                    models.object.addInstance("flip", glm::mat4(1.0))});

                hinge = new btHingeConstraint(*body, btVector3(0,0,0), vUp, false);
                hinge->setLimit (M_PI - 0.6, M_PI + 0.5, 0.001f, 1.0f, -0.001f);
                dynamicsWorld->addConstraint(hinge);
                //left
                body = addRigidBody (shape, 0x02,0x01, 0.1, 0.2, mass,
                            slopeRotMatrix *
                            btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.09194,0.01479,0.39887)));

                flippers.push_back({body,
                    models.object.addInstance("flip", glm::mat4(1.0))});

                hinge = new btHingeConstraint (*body, btVector3(0,0,0), vUp, false);
                hinge->setLimit (-0.5, 0.6, .001f,1.0f,-0.001f);
                dynamicsWorld->addConstraint(hinge);
                //top left
                shape = getConvexHullShape(modBodies, i, 0.92f);
                shape->calculateLocalInertia(mass, fallInertia);
                body = addRigidBody (shape, 0x02,0x01, 0.1, 0.2, mass,
                            slopeRotMatrix *
                            btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.22988,0.01479,-0.07538)));

                flippers.push_back({body,
                    models.object.addInstance("flip", glm::mat4(1.0))});

                hinge = new btHingeConstraint (*body, btVector3(0,0,0), vUp, false);
                hinge->setLimit (-1.20, 0., .001f,1.0f,-0.001f);
                dynamicsWorld->addConstraint(hinge);
                break;
            case str2int("guide-lp")://guide
                shape = getConvexHullShape(modBodies, i);
                createGuide(shape, btVector3(-0.188,0,0.248),0);
                createGuide(shape, btVector3(0.188,0,0.248),0);
                break;
            case str2int("door1-lp")://doors
                shape = getConvexHullShape(modBodies, i);
                mass = 0.003;
                shape->calculateLocalInertia(mass, fallInertia);
                createDoor (shape, mass, btVector3(0.28105,0,-0.27905), 0.148, 1., -0.0001);
                createDoor (shape, mass, btVector3(-0.06332,0,-0.5206), M_PI_2+0.1, M_PI_2 + 0.6, -0.0001);
                createDoor (shape, mass, btVector3(0.05674,0,-0.5206), M_PI_2-0.38, M_PI_2 + 0.6, -0.0001);
                break;
            case str2int("bump-lp")://bumper
            {    //bumpers
                btVector3 bumperPos[] = {
                    btVector3(-0.099, 0.0,-0.307),
                    btVector3(-0.017, 0.0,-0.239),
                    btVector3(0.058, 0.0,-0.307),
                };
                shape = getConvexHullShape(modBodies, i);
                for (int j = 0; j<3 ; j++) {
                    trBody = slopeRotMatrix * btTransform(btQuaternion(0, 0, 0, 1), bumperPos[j]);
                    body = addRigidBody (shape, 0x02,0x01, 1.2, 0.0, 0, trBody);
                    body->setUserIndex(BUMP_BDY_ID);
                    body->setWorldTransform (trBody);
                    bumpers.push_back({body,
                        models.object.addInstance("bumper", btTransformToGlmMat(trBody))});
                }
                break;
            }
            case str2int("spinner-lp")://spinner
                shape = getConvexHullShape(modBodies, i);
                mass = 0.01;
                shape->calculateLocalInertia(mass, fallInertia);
                trBody = slopeRotMatrix *
                        btTransform(btQuaternion(btVector3(0,1,0), -21.f * M_PI / 180.0), btVector3(0.24554,0.02731,-0.13162));
                body = addRigidBody (shape, 0x02,0x01, 0., 0.01, mass, trBody);
                body->setUserIndex(SPIN_BDI_ID);
                body->setUserIndex2(1);

                spinners.push_back({body,
                    models.object.addInstance("spinner-hr", btTransformToGlmMat(trBody))});
                models.object.addInstance("spinnerBord", btTransformToGlmMat(trBody));

                hinge = new btHingeConstraint (*body, btVector3(0,0.006,0), btVector3(1,0,0), false);
                hinge->setLimit (0., 2 * M_PI, 0.1f, 0.f,-1.0f);
                dynamicsWorld->addConstraint(hinge);
                break;
            case str2int("target1-lp")://target1
                shape = getConvexHullShape(modBodies, i);

                leftTargets = TargetGroup(TARG_LEFT_BDY_ID,btVector3(-0.222, 0.0, 0.032), 71.0 * M_PI/180.0, 0.003);

                for (int i = 0; i < 4; i++)
                    leftTargets.targets.push_back(
                                Target(addRigidBody (shape, 0x02,0x01, 0.5, 0.1),
                                models.object.addInstance("target1", glm::mat4()),leftTarget_points * i));



                rightTargets = TargetGroup(TARG_RIGHT_BDY_ID,btVector3(0.276, 0.0, 0.0214), -M_PI_2, 0.003);

                for (int i = 0; i < 4; i++)
                    rightTargets.targets.push_back(
                                Target(addRigidBody (shape, 0x02,0x01, 0.5, 0.1),
                                models.object.addInstance("target1", glm::mat4()),leftTarget_points * i));

                leftTargets.createBodies(models.object);
                rightTargets.createBodies(models.object);

                break;
            default:
                std::cout << modBodies.primitives[i].name << std::endl << std::flush;
                break;
            }
        }

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
        info.m_numIterations = iterations;
        info.m_splitImpulse = int(splitImpulse);

        initPhysicalBodies();

    }

    void update_physics () {
        float increment = 1.f/100000.f;


        if (CHECK_BIT(functionKeyState, 0)) {//F1
            fixedTimeStepDiv -= increment;
            if (fixedTimeStepDiv <= 0.f)
                fixedTimeStepDiv = increment;
        }
        if (CHECK_BIT(functionKeyState, 1)) {//F2
            fixedTimeStepDiv += increment;
            if (fixedTimeStepDiv <= 0.f)
                fixedTimeStepDiv = increment;
        }
        if (CHECK_BIT(functionKeyState, 2)) {//F3
            timeStep -= increment;
            if (timeStep <= 0.f)
                timeStep = increment;
        }
        if (CHECK_BIT(functionKeyState, 3)) {//F4
            timeStep += increment;
            if (timeStep <= 0.f)
                timeStep = increment;
        }

        btTransform trans;
        for (int i=0; i<doors.size(); i++) {
            doors[i].body->getMotionState()->getWorldTransform(trans);
            models.object.instanceDatas[doors[i].instanceIdx].modelMat = btTransformToGlmMat(trans);
        }
        for (int i=0; i < balls.size(); i++) {
            balls[i].body->getMotionState()->getWorldTransform(trans);
            models.object.instanceDatas[balls[i].instanceIdx].modelMat = btTransformToGlmMat(trans);
        }
        for (int i=0; i < flippers.size(); i++) {
            flippers[i].body->getMotionState()->getWorldTransform(trans);
            models.object.instanceDatas[flippers[i].instanceIdx].modelMat = btTransformToGlmMat(trans);
        }
        for (int i=0; i < spinners.size(); i++) {
            spinners[i].body->getMotionState()->getWorldTransform(trans);
            models.object.instanceDatas[spinners[i].instanceIdx].modelMat = btTransformToGlmMat(trans);
        }

        //scale top left flipper
        models.object.instanceDatas[flippers[2].instanceIdx].modelMat =
                models.object.instanceDatas[flippers[2].instanceIdx].modelMat *
                glm::scale(glm::mat4(1.0), glm::vec3(0.92,0.92,0.92));

        models.object.updateInstancesBuffer();
    }

    void step_physics () {

        for (int i=0; i<doors.size(); i++)
            doors[i].body->applyTorqueImpulse(btVector3(0,1,0)* doors[i].strength);

        btVector3 avSpinner = spinners[0].body->getAngularVelocity();
        if (avSpinner.getX() != 0) {
            avSpinner.setX(avSpinner.getX()*0.999);
            spinners[0].body->setAngularVelocity (avSpinner);
        }

        if (leftFlip) {
            flippers[1].body->applyTorqueImpulse(btVector3(0,1,0)* flipperStrength);
            flippers[2].body->applyTorqueImpulse(btVector3(0,1,0)* flipperStrength);
        }else {
            flippers[1].body->applyTorqueImpulse(btVector3(0,-1,0)* flipperStrength);
            flippers[2].body->applyTorqueImpulse(btVector3(0,-1,0)* flipperStrength);
        }

        if (rightFlip) {
            flippers[0].body->applyTorqueImpulse(btVector3(0,1,0)* -flipperStrength);
        }else {
            flippers[0].body->applyTorqueImpulse(btVector3(0,1,0)* flipperStrength);
        }

        clock_t time = clock();

        lastTimeStep = float(diffclock(time, lastTime));

        if (lastTimeStep > timeStep) {
            lastTime = time;
            //dynamicsWorld->updateAabbs();
            subSteps = dynamicsWorld->stepSimulation(lastTimeStep,maxSubsteps,fixedTimeStepDiv);
            update_physics();
            check_collisions(dynamicsWorld, this);

            leftTargets.checkStates(time, models.object);
            rightTargets.checkStates(time, models.object);
        }

        //dynamicsWorld->stepSimulation(1.0/1600.0,10,1.0/1000.0);


    }

    void render () {
        if (!prepared)
            return;
#if BT_DEBUG_DRAW
        dynamicsWorld->debugDrawWorld();
        debugRenderer->generateText(std::to_string(player_points),btVector3(0.5,0.2,-1.0),0.2f);

        debugRenderer->drawLine(btVector3(0,0,0), btVector3(1,0,0), btVector3(1,0,0));

        char string[200] = {};

        sprintf(string, "%.5f ms/frame (%d fps)", 1000.0f / (float)lastFPS, lastFPS);
        debugRenderer->generateText(std::string(string),btVector3(-0.7,0.1,-0.4),0.03f);
        sprintf(string, "Time step:%.5f Iterations:%d (last step: %.5f)", timeStep, iterations, lastTimeStep);
        debugRenderer->generateText(std::string(string),btVector3(-0.7,0.07,-0.4),0.03f);
        sprintf(string, "Fixed time step:%.5f", fixedTimeStepDiv);
        debugRenderer->generateText(std::string(string),btVector3(-0.7,0.04,-0.4),0.03f);
        sprintf(string, "Sub Step: %d (max = %d)", subSteps, maxSubsteps);
        debugRenderer->generateText(std::string(string),btVector3(-0.7,0.01,-0.4),0.03f);

        debugRenderer->flushLines();
        debugRenderer->buildCommandBuffer ();
#endif

        prepareFrame();

        this->submit(queue, &presentCompleteSemaphore, 1);

#if BT_DEBUG_DRAW
        debugRenderer->submit(queue,&this->drawComplete, 1);
        VK_CHECK_RESULT(swapChain.queuePresent(queue, debugRenderer->drawComplete));
#else
        VK_CHECK_RESULT(swapChain.queuePresent(queue, this->drawComplete));
#endif
        step_physics();

        vkDeviceWaitIdle(device);
    }

    uint32_t functionKeyState = 0;
    virtual void keyDown(uint32_t key) {
        switch (key) {
        case 37://left ctrl
            leftFlip = true;
            flippers[1].body->activate();
            flippers[2].body->activate();
            break;
        case 105://right ctrl
            rightFlip = true;
            flippers[0].body->activate();
            break;
        }
        if (key > 66 && key < 80)
            functionKeyState |= 1 << (key - 67);
    }
    virtual void keyUp(uint32_t key) {
        switch (key) {
        case 37://left ctrl
            leftFlip = false;
            flippers[1].body->activate();
            flippers[2].body->activate();
            break;
        case 105://right ctrl
            rightFlip = false;
            flippers[0].body->activate();
            break;
        }
        if (key > 66 && key < 80)
            functionKeyState &= ~(1 << (key - 67));
    }
    virtual void keyPressed(uint32_t key) {
        switch (key) {
//        case 37://left ctrl
//            worldObjs[2].body->clearForces();
//            break;
//        case 105://right ctrl
//            worldObjs[1].body->clearForces();
//            break;
        case 71://f5
            iterations -= 1;
            dynamicsWorld->getSolverInfo().m_numIterations = iterations;
            break;
        case 72:
            iterations += 1;
            dynamicsWorld->getSolverInfo().m_numIterations = iterations;
            break;
        case 73://f7
            maxSubsteps -= 1;
            break;
        case 74:
            maxSubsteps += 1;
            break;
        case 65:
            balls[0].body->setLinearVelocity(balls[0].body->getLinearVelocity() + pushDir);
            balls[0].body->activate();
            break;
        case 79://7
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0.085,0.08,-0.431)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 80://8
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.085,0.02,0.331)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 81://9
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0.085,0.01,0.365)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 83://4
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.215,0.01,0.17)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 84://5
            //pushDir = btVector3(1,0,0);
            //balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.4457,0.05,0.535)));
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0.298,0.05,0.2)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 85://6
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0.215,0.01,0.17)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 87://1
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.11,0.02,0.22)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 88:
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0.,0.02,0.)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 89://3
            balls[0].body->setWorldTransform(btTransform(btQuaternion(0, 0, 0, 1), btVector3(-0.195,0.025,-0.10)));
            balls[0].body->setLinearVelocity(btVector3(0,0,0));
            break;
        case 90://0
//            rebuildCommandBuffers();
            break;
        }
    }
};

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

void check_collisions (btDynamicsWorld *dynamicsWorld, void *app) {
    VulkanExample* vkapp = (VulkanExample*) app;

    for (int j = dynamicsWorld->getNumCollisionObjects() - 1; j >= 0; --j) {
        btCollisionObject *obj = dynamicsWorld->getCollisionObjectArray()[j];

        btRigidBody *body = btRigidBody::upcast(obj);

        int bdyId = body->getUserIndex();

        switch (bdyId) {
        case BUMP_BDY_ID:
        {
            std::vector<btManifoldPoint*>& manifoldPoints = objectsCollisions[body];
            if (manifoldPoints.size()==0)
                continue;
            vkapp->balls[0].body->applyImpulse(-manifoldPoints[0]->m_normalWorldOnB * damperStrength,btVector3(0,0,0));
                        vkapp->player_points += vkapp->bumper_points;
            break;
        }
        {
            case TARG_LEFT_BDY_ID:
            case TARG_RIGHT_BDY_ID:
                std::vector<btManifoldPoint*>& manifoldPoints = objectsCollisions[body];
                if (manifoldPoints.size()==0)
                    continue;
                int targ = body->getUserIndex2();
                VulkanExample::TargetGroup* tg;

                if (bdyId == TARG_LEFT_BDY_ID)
                    tg = &vkapp->leftTargets;
                else if (bdyId == TARG_RIGHT_BDY_ID)
                    tg = &vkapp->rightTargets;

                if (tg->targets[targ].state)
                    continue;
                obj->setActivationState(DISABLE_SIMULATION);
                tg->targets[targ].state = true;

                vkapp->models.object.instanceDatas[tg->targets[targ].instanceIdx].modelMat *=
                        glm::translate (glm::mat4(), glm::vec3(0,+0.2,0));

                tg->reachedTargetCount++;
                vkapp->player_points += tg->targets[targ].points;

                if (tg->reachedTargetCount == tg->targets.size())
                    tg->reachedTime = clock();

                break;
        }
        case DAMP_BDY_ID:
        {
            std::vector<btManifoldPoint*>& manifoldPoints = objectsCollisions[body];
            if (manifoldPoints.size()==0)
                continue;
            //btVector3 vDir = manifoldPoints[0]->getPositionWorldOnA() - manifoldPoints[0]->getPositionWorldOnB();
            vkapp->balls[0].body->applyImpulse(-manifoldPoints[0]->m_normalWorldOnB * damperStrength,btVector3(0,0,0));
            vkapp->player_points += vkapp->damper_points;
            break;
        }
        }
    }
}


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
