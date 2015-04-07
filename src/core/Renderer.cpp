#include "Renderer.h"



#define FOG_EQUATION_LINEAR 0
#define FOG_EQUATION_EXP    1
#define FOG_EQUATION_EXP2   2
#define FOG_EQUATION_NONE   3

#ifndef __FOGPARAMS__
#define __FOGPARAMS__
namespace FogParams {
	float fDensity = 0.001f;
	float fStart = 50.0f;
	float fEnd = 5000.0f;
	float3 vColor = make_vector(1.0f, 1.0f, 1.0f);
	int fEquation = FOG_EQUATION_LINEAR;
};
#endif //__FOGPARAMS__


Renderer::Renderer(int argc, char *argv[], int width, int height) : width(width), height(height)
{
#	if defined(__linux__)
	linux_initialize_cwd();
#	endif // ! __linux__


	glutInit(&argc, argv);


#	if defined(GLUT_SRGB)
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_SRGB | GLUT_DEPTH);
#	else // !GLUT_SRGB
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	printf("--\n");
	printf("-- WARNING: your GLUT doesn't support sRGB / GLUT_SRGB\n");
#	endif // ~ GLUT_SRGB
	glutInitWindowSize(width, height);


	glutInitContextVersion(3, 0);
	glutInitContextFlags(GLUT_DEBUG);

	glutCreateWindow("Project");
	/* If sRGB is available, enable rendering in sRGB. Note: we should do
	* this *after* initGL(), since initGL() initializes GLEW.
	*/
	glEnable(GL_FRAMEBUFFER_SRGB);

}


Renderer::~Renderer()
{
}

void Renderer::start() {
	glutMainLoop();
}

void Renderer::render() {
	glutPostRedisplay();
}

void Renderer::setDisplayMethod(void(*display)(void)) {
	glutDisplayFunc(display);
}

void Renderer::setIdleMethod(void(*idle)(int), float delay) {
	glutTimerFunc(delay, idle, 0);
}

void Renderer::drawModel(Mesh &model, GLuint shaderProgram)
{
	setUniformSlow(shaderProgram, "modelMatrix", model.m_modelMatrix);
	model.render();
}

void Renderer::drawScene(Camera camera, Scene scene, float currentTime)
{
	Renderer::currentTime = currentTime;

	float4x4 viewMatrix = camera.getViewMatrix();
	float4x4 projectionMatrix = camera.getProjectionMatrix();

	// enable back face culling.
	glEnable(GL_CULL_FACE);

	//*************************************************************************
	// Render shadow map
	//*************************************************************************

	int w = glutGet((GLenum)GLUT_WINDOW_WIDTH);
	int h = glutGet((GLenum)GLUT_WINDOW_HEIGHT);

	float4x4 lightViewMatrix = scene.sun->getViewMatrix();
	float4x4 lightProjectionMatrix = scene.sun->getProjectionMatrix();
	float4x4 lightViewProjectionMatrix = lightProjectionMatrix * lightViewMatrix;

	float4x4 lightMatrix =
		make_translation({ 0.5, 0.5, 0.5 }) *
		make_scale<float4x4>(make_vector(0.5f, 0.5f, 0.5f)) *
		lightViewProjectionMatrix * inverse(viewMatrix);

	drawShadowMap(sbo, lightViewProjectionMatrix, scene);
	glBindFramebuffer(GL_FRAMEBUFFER, postProcessFbo.id);

	//*************************************************************************
	// Render the scene from the cameras viewpoint, to the default framebuffer
	//*************************************************************************
	glClearColor(0.2, 0.2, 0.8, 1.0);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, w, h);
	// Use shader and set up uniforms
	glUseProgram(shaderProgram);

	//Sets matrices
	setUniformSlow(shaderProgram, "viewMatrix", viewMatrix);
	setUniformSlow(shaderProgram, "projectionMatrix", projectionMatrix);
	setUniformSlow(shaderProgram, "lightpos", scene.sun->getPosition());
	setUniformSlow(shaderProgram, "lightMatrix", lightMatrix);
	setUniformSlow(shaderProgram, "inverseViewNormalMatrix", transpose(viewMatrix));

	//Sets fog
	setFog(shaderProgram);

	//Set shadowmap
	setUniformSlow(shaderProgram, "shadowMap", 1);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, sbo.texture);

	//Set cube map
	setUniformSlow(shaderProgram, "cubeMap", 2);

	if (scene.cubeMap != NULL) {
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_CUBE_MAP, scene.cubeMap->texture);
	}

	//Render models
	drawShadowCasters(shaderProgram, scene);

	//Render debuggers
	drawDebug(viewMatrix, projectionMatrix, scene);

	//Render skybox
	if (scene.skybox != NULL) {
		scene.skybox->render();
	}

	//Perform post processing
	renderPostProcess();

	//Cleanup
	glUseProgram(0);
}

/**
* In this function, add all scene elements that should cast shadow, that way
* there is only one draw call to each of these, as this function is called twice.
*/
void Renderer::drawShadowCasters(GLuint shaderProgram, Scene scene)
{
	if (scene.car != NULL) {
		setUniformSlow(shaderProgram, "object_reflectiveness", 1.5f);
		drawModel(*scene.car, shaderProgram);
	}

	setUniformSlow(shaderProgram, "object_reflectiveness", 0.0f);
	for (int i = 0; i < scene.shadowCasters.size(); i++) {
		drawModel(*scene.shadowCasters[i], shaderProgram);
	}
}

void Renderer::drawShadowMap(Fbo sbo, float4x4 viewProjectionMatrix, Scene scene) {
	glBindFramebuffer(GL_FRAMEBUFFER, sbo.id);
	glViewport(0, 0, SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION);

	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.5, 2.0);

	GLint currentProgram;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
	glUseProgram(sbo.shaderProgram);

	setUniformSlow(sbo.shaderProgram, "viewProjectionMatrix", viewProjectionMatrix);

	drawShadowCasters(sbo.shaderProgram, scene);

	//CLEANUP
	glDisable(GL_POLYGON_OFFSET_FILL);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(currentProgram);
}

void Renderer::setFog(GLuint shaderProgram) {
	setUniformSlow(shaderProgram, "fog.iEquation", FogParams::fEquation);
	setUniformSlow(shaderProgram, "fog.fDensity", FogParams::fDensity);
	setUniformSlow(shaderProgram, "fog.fEnd", FogParams::fEnd);
	setUniformSlow(shaderProgram, "fog.fStart", FogParams::fStart);
	setUniformSlow(shaderProgram, "fog.vColor", FogParams::vColor);
}

void Renderer::initGL() 
{
	int w = width;
	int h = height;


	/* Initialize GLEW; this gives us access to OpenGL Extensions.
	*/
	glewInit();

	/* Print information about OpenGL and ensure that we've got at a context
	* that supports least OpenGL 3.0. Then setup the OpenGL Debug message
	* mechanism.
	*/
	startupGLDiagnostics();
	setupGLDebugMessages();

	/* Initialize DevIL, the image library that we use to load textures. Also
	* tell IL that we intent to use it with OpenGL.
	*/
	ilInit();
	ilutRenderer(ILUT_OPENGL);

	/* Workaround for AMD. It might no longer be necessary, but I dunno if we
	* are ever going to remove it. (Consider it a piece of living history.)
	*/
	if (!glBindFragDataLocation)
	{
		glBindFragDataLocation = glBindFragDataLocationEXT;
	}

	//*************************************************************************
	//	Load shaders
	//*************************************************************************
	shaderProgram = loadShaderProgram("shaders/simple.vert", "shaders/simple.frag");
	glBindAttribLocation(shaderProgram, 0, "position");
	glBindAttribLocation(shaderProgram, 2, "texCoordIn");
	glBindAttribLocation(shaderProgram, 1, "normalIn");
	glBindFragDataLocation(shaderProgram, 0, "fragmentColor");
	linkShaderProgram(shaderProgram);


	//*************************************************************************
	// Generate shadow map frame buffer object
	//*************************************************************************
	logger.logInfo("Generating OpenGL data.");

	sbo.shaderProgram = loadShaderProgram("shaders/shadowMap.vert", "shaders/shadowMap.frag");
	glBindAttribLocation(sbo.shaderProgram, 0, "position");
	glBindFragDataLocation(sbo.shaderProgram, 0, "fragmentColor");
	linkShaderProgram(sbo.shaderProgram);

	sbo.width = SHADOW_MAP_RESOLUTION;
	sbo.height = SHADOW_MAP_RESOLUTION;

	glGenFramebuffers(1, &sbo.id);
	glBindFramebuffer(GL_FRAMEBUFFER, sbo.id);

	glGenTextures(1, &sbo.texture);
	glBindTexture(GL_TEXTURE_2D, sbo.texture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

	float4 zeros = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &zeros.x);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sbo.texture, 0);

	//Disable reading color buffer
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);

	//Cleanup
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);


	//*************************************************************************
	// Create post process Fbo
	//*************************************************************************
	postFxShader = loadShaderProgram("shaders/postFx.vert", "shaders/postFx.frag");
	verticalBlurShader = loadShaderProgram("shaders/postFx.vert", "shaders/vertical_blur.frag");
	horizontalBlurShader = loadShaderProgram("shaders/postFx.vert", "shaders/horizontal_blur.frag");
	cutoffShader = loadShaderProgram("shaders/postFx.vert", "shaders/cutoff.frag");

	glBindAttribLocation(postFxShader, 0, "position");
	glBindFragDataLocation(postFxShader, 0, "fragmentColor");

	linkShaderProgram(postFxShader);
	linkShaderProgram(verticalBlurShader);
	linkShaderProgram(horizontalBlurShader);
	linkShaderProgram(cutoffShader);

	postProcessFbo = createPostProcessFbo(width, height);
	verticalBlurFbo = createPostProcessFbo(width, height);
	horizontalBlurFbo = createPostProcessFbo(width, height);
	cutOffFbo = createPostProcessFbo(width, height);

		
	//Cleanup
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_DEPTH_TEST);

	logger.logInfo("Generating OpenGL data completed.");
}

Fbo Renderer::createPostProcessFbo(int width, int height) {

	Fbo fbo;

	fbo.width = width;
	fbo.height = height;

	glGenFramebuffers(1, &fbo.id);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo.id);

	glGenTextures(1, &fbo.texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, fbo.texture);

	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_ARB, fbo.texture, 0);

	glGenRenderbuffers(1, &fbo.depthbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, fbo.depthbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo.depthbuffer);

	return fbo;
}


void Renderer::renderPostProcess() {
	int w = glutGet((GLenum)GLUT_WINDOW_WIDTH);
	int h = glutGet((GLenum)GLUT_WINDOW_HEIGHT);

	blurImage();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, w, h);
	glClearColor(0.6, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(postFxShader);
	setUniformSlow(postFxShader, "frameBufferTexture", 0);
	setUniformSlow(postFxShader, "blurredFrameBufferTexture", 1);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, postProcessFbo.texture);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, verticalBlurFbo.texture);

	setUniformSlow(postFxShader, "time", currentTime);

	drawFullScreenQuad();
}

void Renderer::blurImage() {
	//CUTOFF
	glUseProgram(cutoffShader);
	glBindFramebuffer(GL_FRAMEBUFFER, cutOffFbo.id);
	glViewport(0, 0, width, height);
	glClearColor(1.0, 1.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	setUniformSlow(cutoffShader, "time", currentTime);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, postProcessFbo.texture);

	drawFullScreenQuad();

	//HORIZONTAL
	glBindFramebuffer(GL_FRAMEBUFFER, horizontalBlurFbo.id);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(horizontalBlurShader);

	setUniformSlow(horizontalBlurShader, "frameBufferTexture", 0);
	setUniformSlow(horizontalBlurShader, "time", currentTime);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, cutOffFbo.texture);
	drawFullScreenQuad();

	//VERTICAL
	glBindFramebuffer(GL_FRAMEBUFFER, verticalBlurFbo.id);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(verticalBlurShader);

	setUniformSlow(verticalBlurShader, "frameBufferTexture", 0);
	setUniformSlow(verticalBlurShader, "time", currentTime);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, horizontalBlurFbo.texture);
	drawFullScreenQuad();
}

void Renderer::drawFullScreenQuad()
{
	static GLuint vertexArrayObject = 0;
	static int nofVertices = 4;

	// do this initialization first time the function is called... somewhat dodgy, but works for demonstration purposes
	if (vertexArrayObject == 0)
	{
		glGenVertexArrays(1, &vertexArrayObject);
		static const float2 positions[] = {
				{ -1.0f, -1.0f },
				{ 1.0f, -1.0f },
				{ 1.0f, 1.0f },
				{ -1.0f, 1.0f },
		};
		createAddAttribBuffer(vertexArrayObject, positions, sizeof(positions), 0, 2, GL_FLOAT);
	}

	glBindVertexArray(vertexArrayObject);
	glDrawArrays(GL_QUADS, 0, nofVertices);
}



void Renderer::drawDebug(const float4x4 &viewMatrix, const float4x4 &projectionMatrix, Scene scene) {
	//debugDrawOctree(viewMatrix, projectionMatrix, t);
	debugDrawLight(viewMatrix, projectionMatrix, scene.sun->getPosition() );
	/*debugDrawQuad(viewMatrix, projectionMatrix, carLoc.location + make_vector(0.2f, 1.2f, 0.0f), make_vector(1.0f, 1.0f, 1.5f));
	float3x3 rot = make_rotation_y<float3x3>(carLoc.angley);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location + rot * carLoc.wheel1, -carLoc.upDir);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location + rot * carLoc.wheel2, -carLoc.upDir);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location + rot * carLoc.wheel3, -carLoc.upDir);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location + rot * carLoc.wheel4, -carLoc.upDir);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location, carLoc.upDir);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location, carLoc.frontDir);
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location, normalize(cross(carLoc.frontDir, make_vector(0.0f, 1.0f, 0.0f))));
	debugDrawLine(viewMatrix, projectionMatrix, carLoc.location, vUp);*/
}

void Renderer::debugDrawLine(const float4x4 &viewMatrix, const float4x4 &projectionMatrix, float3 origin, float3 rayVector) {
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	GLint temp;
	glColor3f(1.0, 1.0, 0.0);
	glGetIntegerv(GL_CURRENT_PROGRAM, &temp);
	glUseProgram(0);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(&projectionMatrix.c1.x);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(&viewMatrix.c1.x);

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	float3 p[8];
	p[0] = origin;
	p[1] = origin + (normalize(rayVector) * 5);

	glBegin(GL_LINES);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glEnd();
	glUseProgram(temp);
	glPopAttrib();
}

void Renderer::debugDrawQuad(const float4x4 &viewMatrix, const float4x4 &projectionMatrix, float3 origin, float3 halfVector) {
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	GLint temp;
	glColor3f(1.0, 1.0, 0.0);
	glGetIntegerv(GL_CURRENT_PROGRAM, &temp);
	glUseProgram(0);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(&projectionMatrix.c1.x);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(&viewMatrix.c1.x);

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	float3 p[8];
	p[0] = origin + (halfVector * make_vector(1.0f, 1.0f, 1.0f));
	p[1] = origin + (halfVector * make_vector(-1.0f, 1.0f, 1.0f));

	p[2] = origin + (halfVector * make_vector(1.0f, -1.0f, 1.0f));
	p[3] = origin + (halfVector * make_vector(-1.0f, -1.0f, 1.0f));

	p[4] = origin + (halfVector * make_vector(1.0f, 1.0f, -1.0f));
	p[5] = origin + (halfVector * make_vector(-1.0f, 1.0f, -1.0f));

	p[6] = origin + (halfVector * make_vector(1.0f, -1.0f, -1.0f));
	p[7] = origin + (halfVector * make_vector(-1.0f, -1.0f, -1.0f));


	glBegin(GL_LINES);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[2].x, p[2].y, p[2].z);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[4].x, p[4].y, p[4].z);

	///////
	glVertex3f(p[7].x, p[7].y, p[7].z);
	glVertex3f(p[6].x, p[6].y, p[6].z);

	glVertex3f(p[7].x, p[7].y, p[7].z);
	glVertex3f(p[5].x, p[5].y, p[5].z);

	glVertex3f(p[7].x, p[7].y, p[7].z);
	glVertex3f(p[3].x, p[3].y, p[3].z);

	///////
	glVertex3f(p[5].x, p[5].y, p[5].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glVertex3f(p[5].x, p[5].y, p[5].z);
	glVertex3f(p[4].x, p[4].y, p[4].z);

	///////
	glVertex3f(p[3].x, p[3].y, p[3].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glVertex3f(p[3].x, p[3].y, p[3].z);
	glVertex3f(p[2].x, p[2].y, p[2].z);

	///////
	glVertex3f(p[6].x, p[6].y, p[6].z);
	glVertex3f(p[4].x, p[4].y, p[4].z);

	glVertex3f(p[6].x, p[6].y, p[6].z);
	glVertex3f(p[2].x, p[2].y, p[2].z);

	glEnd();
	glUseProgram(temp);
	glPopAttrib();
}

void Renderer::debugDrawOctree(const float4x4 &viewMatrix, const float4x4 &projectionMatrix, Octree tree)
{
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	GLint temp;
	glColor3f(1.0, 1.0, 0.0);
	glGetIntegerv(GL_CURRENT_PROGRAM, &temp);
	glUseProgram(0);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(&projectionMatrix.c1.x);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(&viewMatrix.c1.x);

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	float3 p[8];
	p[0] = tree.origin + (tree.halfVector * make_vector(1.0f, 1.0f, 1.0f));
	p[1] = tree.origin + (tree.halfVector * make_vector(-1.0f, 1.0f, 1.0f));

	p[2] = tree.origin + (tree.halfVector * make_vector(1.0f, -1.0f, 1.0f));
	p[3] = tree.origin + (tree.halfVector * make_vector(-1.0f, -1.0f, 1.0f));

	p[4] = tree.origin + (tree.halfVector * make_vector(1.0f, 1.0f, -1.0f));
	p[5] = tree.origin + (tree.halfVector * make_vector(-1.0f, 1.0f, -1.0f));

	p[6] = tree.origin + (tree.halfVector * make_vector(1.0f, -1.0f, -1.0f));
	p[7] = tree.origin + (tree.halfVector * make_vector(-1.0f, -1.0f, -1.0f));


	glBegin(GL_LINES);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[2].x, p[2].y, p[2].z);

	glVertex3f(p[0].x, p[0].y, p[0].z);
	glVertex3f(p[4].x, p[4].y, p[4].z);

	///////
	glVertex3f(p[7].x, p[7].y, p[7].z);
	glVertex3f(p[6].x, p[6].y, p[6].z);

	glVertex3f(p[7].x, p[7].y, p[7].z);
	glVertex3f(p[5].x, p[5].y, p[5].z);

	glVertex3f(p[7].x, p[7].y, p[7].z);
	glVertex3f(p[3].x, p[3].y, p[3].z);

	///////
	glVertex3f(p[5].x, p[5].y, p[5].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glVertex3f(p[5].x, p[5].y, p[5].z);
	glVertex3f(p[4].x, p[4].y, p[4].z);

	///////
	glVertex3f(p[3].x, p[3].y, p[3].z);
	glVertex3f(p[1].x, p[1].y, p[1].z);

	glVertex3f(p[3].x, p[3].y, p[3].z);
	glVertex3f(p[2].x, p[2].y, p[2].z);

	///////
	glVertex3f(p[6].x, p[6].y, p[6].z);
	glVertex3f(p[4].x, p[4].y, p[4].z);

	glVertex3f(p[6].x, p[6].y, p[6].z);
	glVertex3f(p[2].x, p[2].y, p[2].z);

	glEnd();
	glUseProgram(temp);
	glPopAttrib();


	std::vector<Octree> children;
	tree.getChildren(&children);

	if (children.size() != 0) {
		for (int i = 0; i < 8; i++) {
			debugDrawOctree(viewMatrix, projectionMatrix, children[i]);
		}
	}
}