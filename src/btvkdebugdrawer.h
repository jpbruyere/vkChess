#ifndef BTVKDEBUGDRAWER_H
#define BTVKDEBUGDRAWER_H

#include "vkrenderer.h"
#include "texture.hpp"
#include "LinearMath/btIDebugDraw.h"

class btVKDebugDrawer : public btIDebugDraw, vkRenderer
{
	int m_debugMode;

	VkPipeline              pipelineSDFF;
	VkPipelineLayout        pipelineLayout;
	VkPipelineCache         pipelineCache;

protected:
	std::vector<float>  sdffVertices;
	uint32_t			vBufferSize = 10000 * sizeof(float) * 6;


	virtual void destroy();
	virtual void prepareDescriptors();
	virtual void preparePipeline();

	void sdffAddVertex (float posX, float posY, float posZ, float uvT, float uvU);
	void generateText(const std::string& text, btVector3 pos, float scale);
public:
	std::array<bmchar,255>  fontChars;
	vks::Texture			texSDFFont;

	uint32_t			sdffVertexCount = 0;


	btVKDebugDrawer(vks::VulkanDevice* _device, VulkanSwapChain *_swapChain,
					VkFormat depthFormat, VkSampleCountFlagBits _sampleCount,
					std::vector<VkFramebuffer>&_frameBuffers, vks::Buffer* _uboMatrices,
					std::string fontFnt, vks::Texture &fontTexture);

	virtual ~btVKDebugDrawer();

	virtual void buildCommandBuffer ();

	virtual void flush();
	virtual void clear();

	void clearLines();

	void flushLines();

	virtual void drawLine(const btVector3& from,const btVector3& to,const btVector3& fromColor, const btVector3& toColor);

	virtual void drawLine(const btVector3& from,const btVector3& to,const btVector3& color);

	virtual void drawSphere (const btVector3& p, btScalar radius, const btVector3& color);

	virtual void drawTriangle(const btVector3& a,const btVector3& b,const btVector3& c,const btVector3& color,btScalar alpha);

	virtual void drawContactPoint(const btVector3& PointOnB,const btVector3& normalOnB,btScalar distance,int lifeTime,const btVector3& color);

	virtual void reportErrorWarning(const char* warningString);

	virtual void draw3dText(const btVector3& location,const char* textString);

	virtual void setDebugMode(int debugMode);

	virtual int	getDebugMode() const { return m_debugMode;}



};

#endif // BTVKDEBUGDRAWER_H
