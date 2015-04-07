#ifdef WIN32
#include <windows.h>
#endif


#include <GL/glew.h>
#include <GL/freeglut.h>

#include <IL/il.h>
#include <IL/ilut.h>

#include <stdlib.h>
#include <algorithm>

#include <glutil\glutil.h>
#include <float4x4.h>
#include <float3x3.h>
#include "AABB.h"
#include <Quaternion.h>
#include "collision\Collider.h"

#include <vector>
#include <thread>

#include <ctime>

#include "Octree.h"
#include "Triangle.h"
#include "PerspectiveCamera.h"
#include "Logger.h"
#include <chrono>
#include "Mesh.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include "OBJModel.h"
#include "CubeMapTexture.h"
#include "Skybox.h"
#include "objects\Scene.h"
#include "core\Renderer.h"

using namespace std;
using namespace chag;
using namespace chrono;


#define SCREEN_WIDTH			1028
#define SCREEN_HEIGHT			800

#define TICK_PER_SECOND  50


//*****************************************************************************
//	Global variables
//*****************************************************************************
bool paused = false;				// Tells us wether sun animation is paused
float currentTime = 0.0f;			// Tells us the current time
float timeSinceDraw = 0.0f;

const float3 vUp = make_vector(0.0f, 1.0f, 0.0f);


//*****************************************************************************
//	OBJ Model declarations
//*****************************************************************************
Mesh world;
Mesh worldCollision;
Mesh car;
Mesh factory;
Mesh water;
Mesh spider;

Scene scene;


//*****************************************************************************
//	Cube Maps
//*****************************************************************************
CubeMapTexture* reflectionCubeMap;
Skybox* skybox;


//*****************************************************************************
//	Camera state variables (updated in motion())
//*****************************************************************************
float camera_theta = M_PI / 1.0f;
float camera_phi = M_PI / 4.0f;
float camera_r = 30.0; 
float camera_target_altitude = 5.2; 

//*****************************************************************************
//	Light state variables (updated in idle())
//*****************************************************************************
float3 lightPosition = {30.1f, 450.0f, 0.1f};

//****************************************************************************
//	Input state variables
//*****************************************************************************
bool leftDown = false;
bool middleDown = false;
bool rightDown = false;
int prev_x = 0;
int prev_y = 0;
bool keysDown[256];

Fbo cMapAll;

//*****************************************************************************
//	Camera
//*****************************************************************************

Camera *cubeMapCameras[6];
Camera *sunCamera;
Camera *playerCamera;
int camera = 6;

//*****************************************************************************
//	Car object variables
//*****************************************************************************

struct Car{
	float3 frontDir = make_vector(0.0f, 0.0f, 1.0f);
	float3 upDir = make_vector(0.0f, 1.0f, 0.0f);
	float3 location = make_vector(0.0f, 0.0f, 0.0f);
	float3  
		wheel1 = make_vector( 1.2f, 0.0f,  1.5f),
		wheel2 = make_vector( 1.2f, 0.0f, -1.5f),
		wheel3 = make_vector(-0.8f, 0.0f,  1.5f),
		wheel4 = make_vector(-0.8f, 0.0f, -1.5f);

	float rotationSpeed = 2 * M_PI / 180;
	float moveSpeed = 0.5;
	float angley = 0, anglez, anglex;
	float lengthx = 2, lengthz = 3;
} carLoc;


Renderer *renderer;
//*****************************************************************************
//	Collision objects
//*****************************************************************************
Collider *collider;
Octree *octTree;
bool hasChanged = true;

//***********s******************************************************************
//	Logger
//*****************************************************************************
Logger logger = Logger::instance();


//*****************************************************************************
//	Function declarations
//*****************************************************************************

void drawCubeMap(Fbo fbo, Scene scene);
void createCubeMaps();
void createMeshes();
void createCameras();


float degreeToRad(float degree);
float radToDegree(float rad);
float3 sphericalToCartesian(float theta, float phi, float r);

void updatePlayer();

void checkIntersection() {
	float3 upVec = make_vector(0.0f, 1.0f, 0.0f);

	//Calculate intersections
	float3x3 rot = make_rotation_y<float3x3>(carLoc.angley);
	float a = collider->rayIntersection(carLoc.location + rot * carLoc.wheel1, -upVec);
	float b = collider->rayIntersection(carLoc.location + rot * carLoc.wheel2, -upVec);
	float c = collider->rayIntersection(carLoc.location + rot * carLoc.wheel3, -upVec);
	float d = collider->rayIntersection(carLoc.location + rot * carLoc.wheel4, -upVec);

	if (a == 0 && b == 0 && c == 0 && d == 0) {
		return;
	}
	//Calculate 3d points of intersection
	float3 af = carLoc.wheel1 - (upVec * a);
	float3 bf = carLoc.wheel2 - (upVec * b);
	float3 cf = carLoc.wheel3 - (upVec * c);
	float3 df = carLoc.wheel4 - (upVec * d);

	//Calculate new up vector
	float3 vABa = af - bf;
	float3 vCBa = cf - bf;
	float3 newUpa = cross(vABa, vCBa);

	float3 vABb = bf - cf;
	float3 vCBb = df - cf;
	float3 newUpb = cross(vABb, vCBb);

	float3 halfVector = normalize(newUpa - newUpb);
	carLoc.upDir = -(rot * halfVector);

	

	//Change wheel locations 
	carLoc.wheel1 += upVec * a;
	carLoc.wheel2 += upVec * b;
	carLoc.wheel3 += upVec * c;
	carLoc.wheel4 += upVec * d;

	float3 frontDir = normalize(carLoc.frontDir);
	float3 rightDir = normalize(cross(frontDir, vUp));

	float anglex = -(degreeToRad(90.0f) - acosf(dot(normalize(carLoc.upDir), frontDir)));
	float anglez = (degreeToRad(90.0f) - acosf(dot(normalize(carLoc.upDir), rightDir)));
	
	Quaternion qatX = make_quaternion_axis_angle(rightDir, anglex);
	Quaternion qatZ = make_quaternion_axis_angle(make_rotation_y<float3x3>(-carLoc.angley) * frontDir, anglez);

	float4 newLoc = makematrix(qatX) * makematrix(qatZ) * make_vector4(carLoc.wheel1, 1.0f);

	carLoc.location += make_vector(0.0f, carLoc.wheel1.y + (carLoc.wheel1.y - newLoc.y), 0.0f);
	printf("%f\n", carLoc.wheel1.y + (carLoc.wheel1.y - newLoc.y));
}

void display(void)
{
	Camera *cam;
	if (camera == 6) {
		cam = playerCamera;
	}
	else if (camera == 7) {
		cam = sunCamera;
	}
	else
	{
		cam = cubeMapCameras[camera];
	}
	renderer->drawScene(*cam, scene, currentTime);
	//drawCubeMap(cMapAll, scene);
	
	renderer->swapBuffer();
}

void handleKeys(unsigned char key, int /*x*/, int /*y*/)
{
	switch(key)
	{
	case 27:    /* ESC */
		exit(0); /* dirty exit */
		break;   /* unnecessary, I know */
	case 32:    /* space */
		paused = !paused;
		break;
	case 'w':
	case 'W':
		keysDown[(int)'w'] = true;
		break;
	case 'a':
	case 'A': 
		keysDown[(int)'a'] = true;
		break;
	case 's':   
	case 'S':
		keysDown[(int)'s'] = true;
		break;
	case 'd':   
	case 'D':
		keysDown[(int)'d'] = true;
		break;
	}
}
void handleKeysRelease(unsigned char key, int /*x*/, int /*y*/)
{
	switch (key)
	{
	case 27:    /* ESC */
		exit(0); /* dirty exit */
		break;   /* unnecessary, I know */
	case 32:    /* space */
		paused = !paused;
		break;
	case 'w':
	case 'W':
		keysDown[(int)'w'] = false;
		break;
	case 'a':
	case 'A':
		keysDown[(int)'a'] = false;
		break;
	case 's':
	case 'S':
		keysDown[(int)'s'] = false;
		break;
	case 'd':
	case 'D':
		keysDown[(int)'d'] = false;
		break;
	}
}

void handleSpecialKeys(int key, int /*x*/, int /*y*/)
{
	switch(key)
	{
	case GLUT_KEY_LEFT:
		printf("Left arrow\n");
		camera = (camera + 1) % 8;
		break;
	case GLUT_KEY_RIGHT:
		camera = (camera - 1);
		camera = camera == -1 ? 7 : camera;
		break;
	case GLUT_KEY_UP:
		break;
	case GLUT_KEY_DOWN:
		break;
	}
}

void mouse(int button, int state, int x, int y)
{
	// reset the previous position, such that we only get movement performed after the button
	// was pressed.
	prev_x = x;
	prev_y = y;

	bool buttonDown = state == GLUT_DOWN;

	switch(button)
	{
	case GLUT_LEFT_BUTTON:
		leftDown = buttonDown;
		break;
	case GLUT_MIDDLE_BUTTON:
		middleDown = buttonDown;
		break;
	case GLUT_RIGHT_BUTTON: 
		rightDown = buttonDown;
	default:
		break;
	}
}

void motion(int x, int y)
{
	int delta_x = x - prev_x;
	int delta_y = y - prev_y;

	if(middleDown)
	{
		camera_r -= float(delta_y) * 0.3f;
		// make sure cameraDistance does not become too small
		camera_r = max(0.1f, camera_r);
	}
	if(leftDown)
	{
		camera_phi	-= float(delta_y) * 0.3f * float(M_PI) / 180.0f;
		camera_phi = min(max(0.01f, camera_phi), float(M_PI) - 0.01f);
		camera_theta -= float(delta_x) * 0.3f * float(M_PI) / 180.0f;
	}

	if(rightDown)
	{
		camera_target_altitude += float(delta_y) * 0.1f; 
	}
	prev_x = x;
	prev_y = y;
}

void tick() {
	if (keysDown[(int)'w']) {
		float3 term = carLoc.frontDir * carLoc.moveSpeed;
		carLoc.location += term;
		hasChanged = true;

	}if (keysDown[(int)'s']) {
		float3 term = carLoc.frontDir * carLoc.moveSpeed;
		carLoc.location -= term;
		hasChanged = true;

	}if (keysDown[(int)'a'] && (keysDown[(int)'w'] || keysDown[(int)'s'])) {
		carLoc.angley += carLoc.rotationSpeed;
		carLoc.frontDir = make_rotation_y<float3x3>(carLoc.rotationSpeed) * carLoc.frontDir;
		camera_theta += carLoc.rotationSpeed;
		hasChanged = true;

	}if (keysDown[(int)'d'] && (keysDown[(int)'w'] || keysDown[(int)'s'])) {
		carLoc.angley -= carLoc.rotationSpeed;
		carLoc.frontDir = make_rotation_y<float3x3>(-carLoc.rotationSpeed) * carLoc.frontDir;
		camera_theta -= carLoc.rotationSpeed;
		hasChanged = true;
	}
}

void idle( int v )
{
	float time = (1000 / TICK_PER_SECOND) - (float(glutGet(GLUT_ELAPSED_TIME) - timeSinceDraw));
	if (time < 0) {
		glutTimerFunc(1000 / TICK_PER_SECOND, idle, 0);
		timeSinceDraw = float(glutGet(GLUT_ELAPSED_TIME));
		static float startTime = float(glutGet(GLUT_ELAPSED_TIME)) / 1000.0f;
		// Here is a good place to put application logic.
		currentTime = float(glutGet(GLUT_ELAPSED_TIME)) / 1000.0f - startTime;


		// rotate light around X axis, sunlike fashion.
		// do one full revolution every 20 seconds.
		float4x4 rotateLight = make_rotation_x<float4x4>(2.0f * M_PI * currentTime / 20.0f);
		// rotate and update global light position.
		lightPosition = make_vector3(rotateLight * make_vector(30.1f, 450.0f, 0.1f, 1.0f));
		sunCamera->setPosition(lightPosition);

		//Calculate camera matrix
		playerCamera->setLookAt(carLoc.location + make_vector(0.0f, camera_target_altitude, 0.0f));
		playerCamera->setPosition(carLoc.location + sphericalToCartesian(camera_theta, camera_phi, camera_r));

		tick();

		if (hasChanged){
			checkIntersection();
			updatePlayer();
			hasChanged = false;
		}

		glutPostRedisplay();
	}
	else {
		glutTimerFunc(time, idle, 0);
	}
}


int main(int argc, char *argv[])
{
	int w = SCREEN_WIDTH;
	int h = SCREEN_HEIGHT;

	renderer = new Renderer(argc, argv, w, h);

	glutTimerFunc(50, idle, 0);
	glutDisplayFunc(display);

	glutKeyboardFunc(handleKeys); 
	glutKeyboardUpFunc(handleKeysRelease);
	glutSpecialFunc(handleSpecialKeys); 
	glutMouseFunc(mouse); 
	glutMotionFunc(motion); 

	renderer->initGL();

	createCubeMaps();
	createMeshes();
	createCameras();
	
	renderer->start();

	return 0;
}

void createCubeMaps() {
	int w = SCREEN_WIDTH;
	int h = SCREEN_HEIGHT;
	//*************************************************************************
	// Load cube map texture
	//*************************************************************************
	reflectionCubeMap = new CubeMapTexture("scenes/posx.jpg", "scenes/negx.jpg", "scenes/posy.jpg", "scenes/posy.jpg", "scenes/negz.jpg", "scenes/posz.jpg");

	//X
	cubeMapCameras[0] = new PerspectiveCamera(carLoc.location + make_vector(0.0f, 3.0f, 0.0f), make_vector(100.0f, 3.0f, 0.0f), make_vector(0.0f, -1.0f, 0.0f), 90.0f, 1, 0.1f, 1000.0f);
	cubeMapCameras[1] = new PerspectiveCamera(carLoc.location + make_vector(0.0f, 3.0f, 0.0f), make_vector(-100.0f, 3.0f, 0.0f), make_vector(0.0f, -1.0f, 0.0f), 90.0f, 1, 0.1f, 1000.0f);

	//Y
	cubeMapCameras[2] = new PerspectiveCamera(carLoc.location + make_vector(0.0f, 3.0f, 0.0f), make_vector(0.1f, 100.1f, 0.0f), make_vector(0.0f, -1.0f, 0.0f), 90.0f, 1, 0.1f, 1000.0f);
	cubeMapCameras[3] = new PerspectiveCamera(carLoc.location + make_vector(0.0f, 3.0f, 0.0f), make_vector(0.1f, -100.0f, 0.0f), make_vector(0.0f, -1.0f, 0.0f), 90.0f, 1, 0.1f, 1000.0f);

	//Z
	cubeMapCameras[4] = new PerspectiveCamera(carLoc.location + make_vector(0.0f, 3.0f, 0.0f), make_vector(0.1f, 3.0f, 100.0f), make_vector(0.0f, -1.0f, 0.0f), 90.0f, 1, 0.1f, 1000.0f);
	cubeMapCameras[5] = new PerspectiveCamera(carLoc.location + make_vector(0.0f, 3.0f, 0.0f), make_vector(0.1f, 3.0f, -100.0f), make_vector(0.0f, -1.0f, 0.0f), 90.0f, 1, 0.1f, 1000.0f);

	cMapAll.width = w;
	cMapAll.height = h;

	glGenFramebuffers(1, &cMapAll.id);
	glBindFramebuffer(GL_FRAMEBUFFER, cMapAll.id);

	glGenTextures(1, &cMapAll.texture);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cMapAll.texture);

	glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGBA, CUBE_MAP_RESOLUTION, CUBE_MAP_RESOLUTION, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, GL_RGBA, CUBE_MAP_RESOLUTION, CUBE_MAP_RESOLUTION, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, GL_RGBA, CUBE_MAP_RESOLUTION, CUBE_MAP_RESOLUTION, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, GL_RGBA, CUBE_MAP_RESOLUTION, CUBE_MAP_RESOLUTION, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, GL_RGBA, CUBE_MAP_RESOLUTION, CUBE_MAP_RESOLUTION, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, GL_RGBA, CUBE_MAP_RESOLUTION, CUBE_MAP_RESOLUTION, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

	GLuint depthBuffer;
	glGenRenderbuffers(1, &depthBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, cMapAll.texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, cMapAll.texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, cMapAll.texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, cMapAll.texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, cMapAll.texture, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, cMapAll.texture, 0);

	cMapAll.shaderProgram = loadShaderProgram("shaders/simple.vert", "shaders/simple.frag");
	glBindAttribLocation(cMapAll.shaderProgram, 0, "position");
	glBindAttribLocation(cMapAll.shaderProgram, 2, "texCoordIn");
	glBindAttribLocation(cMapAll.shaderProgram, 1, "normalIn");
	glBindFragDataLocation(cMapAll.shaderProgram, 0, "fragmentColor");
	linkShaderProgram(cMapAll.shaderProgram);
}

void createMeshes() {
	//*************************************************************************
	// Load the models from disk
	//*************************************************************************
	logger.logInfo("Started loading models.");

	//Load shadow casters
	world.loadMesh("scenes/world.obj");
	world.m_modelMatrix = make_identity<float4x4>();
	scene.shadowCasters.push_back(&world);

	factory.loadMesh("scenes/test.obj");
	factory.m_modelMatrix = make_translation(make_vector(-15.0f, 6.0f, 0.0f)) * make_rotation_y<float4x4>(M_PI / 180 * 90) * make_scale<float4x4>(make_vector(2.0f, 2.0f, 2.0f));
	scene.shadowCasters.push_back(&factory);

	spider.loadMesh("scenes/spider.obj");
	spider.m_modelMatrix = make_translation(make_vector(40.0f, 2.0f, 0.0f)) * make_rotation_x<float4x4>(M_PI / 180 * 0) *  make_scale<float4x4>(0.1f);
	scene.shadowCasters.push_back(&spider);

	water.loadMesh("../scenes/water.obj");
	water.m_modelMatrix = make_translation(make_vector(0.0f, -6.0f, 0.0f));
	scene.shadowCasters.push_back(&water);

	logger.logInfo("Finished loading models.");

	//*************************************************************************
	// Generate Octtree from meshes
	//*************************************************************************
	logger.logInfo("Started creating octree");
	high_resolution_clock::time_point start = high_resolution_clock::now();

	float3 origin = make_vector(0.0f, 0.0f, 0.0f);
	float3 halfVector = make_vector(200.0f, 200.0f, 200.0f); //TODO

	octTree = new Octree(origin, halfVector, 0);

	collider = new Collider(octTree);
	collider->addMesh(&world);
	collider->addMesh(&water);
	collider->addMesh(&factory);
	collider->addMesh(&spider);
	collider->insertAll(); //TODO enlargen octrees afterhand instead

	high_resolution_clock::time_point end = high_resolution_clock::now();
	duration<double> time_span = duration_cast<duration<double>>(end - start);

	logger.logInfo("Created octree in : " + std::to_string(time_span.count()) + " seconds.");

}

void createCameras() {
	int w = SCREEN_WIDTH;
	int h = SCREEN_HEIGHT;

	sunCamera = new PerspectiveCamera(lightPosition, make_vector(0.0f, 0.0f, 0.0f), make_vector(0.0f, 1.0f, 0.0f), 45.0f, 1.0f, 280.0f, 600.0f);
	scene.sun = sunCamera;

	playerCamera = new PerspectiveCamera(
		carLoc.location + sphericalToCartesian(camera_theta, camera_phi, camera_r),
		carLoc.location + make_vector(0.0f, camera_target_altitude, 0.0f),
		make_vector(0.0f, 1.0f, 0.0f),
		45.0f, float(w) / float(h), 0.1f, 1000.0f
		);

	car.loadMesh("scenes/car.obj");
	car.m_modelMatrix = make_identity<float4x4>();
	scene.car = &car;

	skybox = new Skybox(playerCamera);
	skybox->init("scenes/posx.jpg", "scenes/negx.jpg", "scenes/posy.jpg", "scenes/posy.jpg", "scenes/negz.jpg", "scenes/posz.jpg");
	scene.skybox = skybox;
}

float degreeToRad(float degree) {
	return degree * M_PI / 180;
}

float radToDegree(float rad) {
	return rad * 180 / M_PI;
}

// Helper function to turn spherical coordinates into cartesian (x,y,z)
float3 sphericalToCartesian(float theta, float phi, float r)
{
	return make_vector(r * sinf(theta)*sinf(phi),
		r * cosf(phi),
		r * cosf(theta)*sinf(phi));
}

/* TIME COUNT
high_resolution_clock::time_point start = high_resolution_clock::now();
high_resolution_clock::time_point end = high_resolution_clock::now();
duration<double> time_span = duration_cast<duration<double>>(end - start);
printf("TIME FOR COLL:%f\n", time_span.count());
*/

void updatePlayer() {
	float3 frontDir = normalize(carLoc.frontDir);
	float3 rightDir = normalize(cross(frontDir, vUp));

	float anglex = -(degreeToRad(90.0f) - acosf(dot(normalize(carLoc.upDir), frontDir)));
	float anglez = (degreeToRad(90.0f) - acosf(dot(normalize(carLoc.upDir), rightDir)));

	Quaternion qatX = make_quaternion_axis_angle(rightDir, anglex);
	Quaternion qatY = make_quaternion_axis_angle(vUp, carLoc.angley);
	Quaternion qatZ = make_quaternion_axis_angle(make_rotation_y<float3x3>(-carLoc.angley) * frontDir, anglez);

	car.m_modelMatrix = make_translation(carLoc.location)
		* makematrix(qatX)
		* makematrix(qatY)
		* makematrix(qatZ);
}