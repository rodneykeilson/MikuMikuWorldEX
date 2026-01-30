#include "Application.h"
#include "ScorePreview.h"
#include "PreviewEngine.h"
#include "Rendering/Camera.h"
#include "ResourceManager.h"
#include "NoteSkin.h"
#include "Colors.h"
#include "ApplicationConfiguration.h"
#include "Tempo.h"
#include "ImageCrop.h"
#include "Note.h"
#include "UI.h"
#include "Localization.h"
#include "ScoreEditorTimeline.h"
#include "InputBinding.h"

namespace MikuMikuWorld
{
	struct PreviewPlaybackState
	{
		bool isPlaying{}, wasLastFramePlaying{};
	} playbackState;

	constexpr float EFFECTS_TARGET_ASPECT = 16.f / 9.f;

	namespace Utils
	{
		// Convert MMWCC Sprite (x, y, width, height) to texture coordinates (x1, x2, y1, y2)
		inline void getSpriteCoords(const Sprite& sprite, float& x1, float& x2, float& y1, float& y2)
		{
			x1 = sprite.getX();
			y1 = sprite.getY();
			x2 = x1 + sprite.getWidth();
			y2 = y1 + sprite.getHeight();
		}

		// Scale a rectangle with specified aspect ratio to be visible inside the target rectangle
		inline void fitRect(float target_width, float target_height, long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width  = target_aspect_ratio > source_aspect_ratio ? source_aspect_ratio * target_height : target_width;
			height = target_aspect_ratio < source_aspect_ratio ? target_width / source_aspect_ratio : target_height;
			return;
		}

		// Scale a rectangle with specified aspect ratio so it fill the area of the target rectangle
		inline void fillRect(float target_width, float target_height, long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width  = target_aspect_ratio < source_aspect_ratio ? source_aspect_ratio * target_height : target_width;
			height = target_aspect_ratio > source_aspect_ratio ? target_width / source_aspect_ratio : target_height;
			return;
		}
	};

	// MMWCC API adaptation helpers
	namespace Adapters
	{
		// Convert XMFLOAT4 array to XMVECTOR array
		inline std::array<DirectX::XMVECTOR, 4> toXMVECTOR(const std::array<DirectX::XMFLOAT4, 4>& arr)
		{
			std::array<DirectX::XMVECTOR, 4> result;
			for (int i = 0; i < 4; i++)
				result[i] = DirectX::XMLoadFloat4(&arr[i]);
			return result;
		}
		
		// Helper to draw quad with XMFLOAT4 array (MMW API) on MMWCC renderer
		// UV order matches MMW's Renderer::setUVCoords for consistency
		inline void drawQuad(Renderer* renderer, const std::array<DirectX::XMFLOAT4, 4>& pos, 
		                     const DirectX::XMMATRIX& model, const Texture& tex,
		                     float x1, float x2, float y1, float y2, const Color& tint, int z)
		{
			// Convert XMFLOAT4 array to XMVECTOR array and get UVs
			std::array<DirectX::XMVECTOR, 4> vPos;
			std::array<DirectX::XMVECTOR, 4> uvCoords;
			
			for (int i = 0; i < 4; i++)
			{
				vPos[i] = DirectX::XMLoadFloat4(&pos[i]);
			}
			
			// Set up UV coordinates - matches MMW Renderer::setUVCoords order:
			// vPos order: [0]=right,bottom [1]=right,top [2]=left,top [3]=left,bottom
			// uvCoords order: [0]=right,top [1]=right,bottom [2]=left,bottom [3]=left,top
			const float texWidth = static_cast<float>(tex.getWidth());
			const float texHeight = static_cast<float>(tex.getHeight());
			const float left = x1 / texWidth;
			const float right = x2 / texWidth;
			const float top = y1 / texHeight;
			const float bottom = y2 / texHeight;
			
			uvCoords[0] = DirectX::XMVectorSet(right, top, 0, 0);
			uvCoords[1] = DirectX::XMVectorSet(right, bottom, 0, 0);
			uvCoords[2] = DirectX::XMVectorSet(left, bottom, 0, 0);
			uvCoords[3] = DirectX::XMVectorSet(left, top, 0, 0);
			
			DirectX::XMVECTOR col = DirectX::XMVectorSet(tint.r, tint.g, tint.b, tint.a);
			
			renderer->pushQuad(vPos, uvCoords, model, col, tex.getID(), z);
		}
	}

	static const int NOTE_SIDE_WIDTH = 91;
	static const int NOTE_SIDE_PAD = 10;
	static const int MAX_FLICK_SPRITES = 6;
	static const int HOLD_XCUTOFF = 36;
	static const int GUIDE_XCUTOFF = 3;
	static const int GUIDE_Y_TOP_CUTOFF = -41;
	static const int GUIDE_Y_BOTTOM_CUTOFF = -12;
	static Color defaultTint { 1.f, 1.f, 1.f, 1.f };

	ScorePreviewBackground::ScorePreviewBackground() : backgroundFile(), jacketFile{}, brightness(0.5f), frameBuffer{2048, 2048}, init{false}
	{

	}

	ScorePreviewBackground::~ScorePreviewBackground()
	{
		frameBuffer.dispose();
	}

	void ScorePreviewBackground::setBrightness(float value)
	{
		brightness = value;
	}

	void ScorePreviewBackground::update(Renderer* renderer, const Jacket& jacket)
	{
		init = true;
		backgroundFile = config.backgroundImage;
		jacketFile = jacket.getFilename();
		brightness = config.pvBackgroundBrightness;
		bool useDefaultTexture = backgroundFile.empty();
		Texture backgroundTex = { useDefaultTexture ? Application::getAppDir() + "res\\textures\\default.png" : backgroundFile};
		const float bgWidth = backgroundTex.getWidth(), bgHeight = backgroundTex.getHeight();
		if (bgWidth != frameBuffer.getWidth() || bgHeight != frameBuffer.getHeight())
			frameBuffer.resize(bgWidth, bgHeight);
		frameBuffer.bind();
		frameBuffer.clear(0, 0, 0, 0);
		int shaderId;
		if ((shaderId = ResourceManager::getShader("basic2d")) == -1) return;
		Shader* basicShader = ResourceManager::shaders[shaderId];
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[ResourceManager::getTexture("stage")];
		basicShader->use();
		basicShader->setMatrix4("projection", Camera::getOffCenterOrthographicProjectionStatic(0, bgWidth, 0, bgHeight));
		renderer->beginBatch();
		renderer->drawRectangle({0, 0}, {bgWidth, bgHeight}, backgroundTex, 0, bgWidth, 0, bgHeight, defaultTint, 0);
		renderer->endBatch();
		if (useDefaultTexture && IO::File::exists(jacket.getFilename()))
			updateDrawDefaultJacket(renderer, jacket);
		frameBuffer.unblind();
		backgroundTex.dispose();
	}

	void ScorePreviewBackground::updateDrawDefaultJacket(Renderer* renderer, const Jacket& jacket)
	{
		if (jacket.getFilename().empty()) return;
		int index = ResourceManager::getTexture("stage");
		if (index == -1) return;
		const Texture& stage = ResourceManager::textures[index];
		const Sprite* sprite;
		if (stage.sprites.size() < STAGE_SPR_COUNT) return;
		int shaderId;
		if ((shaderId = ResourceManager::getShader("basic2d")) == -1) return;
		Shader* basicShader = ResourceManager::shaders[shaderId];
		if ((shaderId = ResourceManager::getShader("masking")) == -1) return;
		Shader* maskShader = ResourceManager::shaders[shaderId];
		
		DirectX::XMVECTOR defCol = DirectX::XMVectorSet(defaultTint.r, defaultTint.g, defaultTint.b, defaultTint.a);
		DirectX::XMVECTOR mainCol = DirectX::XMVectorSet(1, 1, 1, 0.65);
		DirectX::XMVECTOR mirrorCol = DirectX::XMVectorSet(1, 1, 1, 0.35);
		auto mainLeftPos = Engine::quadvPos(602, 602 + 264, 816, 816 + 174);
		auto mainRightPos = Engine::quadvPos(1205, 1205 + 200, 629, 629 + 114);
		auto mirrorLeftPos = Engine::quadvPos(615, 615 + 256, 1170, 1170 + 162);
		auto mirrorRightPos = Engine::quadvPos(1186, 1186 + 196, 1387, 1387 + 105);
		auto mainLeftMask = Engine::quadUV(stage.sprites[SPR_JACKET_LEFT_MASK], stage);
		auto mainRightMask = Engine::quadUV(stage.sprites[SPR_JACKET_RIGHT_MASK], stage);
		auto mirrorLeftMask = Engine::quadUV(stage.sprites[SPR_MIRROR_JACKET_LEFT_MASK], stage);
		auto mirrorRightMask = Engine::quadUV(stage.sprites[SPR_MIRROR_JACKET_RIGHT_MASK], stage);

		maskShader->use();
		maskShader->setInt("baseTex", 0);
		maskShader->setInt("maskTex", 1);
		maskShader->setMatrix4("projection", Camera::getOffCenterOrthographicProjectionStatic(0, 2048, 0, 2048));
		renderer->beginBatch();
		renderer->pushQuadMasked(Adapters::toXMVECTOR(mainLeftPos), Adapters::toXMVECTOR(DefaultJacket::getLeftUV()), Adapters::toXMVECTOR(mainLeftMask), mainCol, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(Adapters::toXMVECTOR(mainRightPos), Adapters::toXMVECTOR(DefaultJacket::getRightUV()), Adapters::toXMVECTOR(mainRightMask), mainCol, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(Adapters::toXMVECTOR(mirrorLeftPos), Adapters::toXMVECTOR(DefaultJacket::getLeftMirrorUV()), Adapters::toXMVECTOR(mirrorLeftMask), mirrorCol, jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(Adapters::toXMVECTOR(mirrorRightPos), Adapters::toXMVECTOR(DefaultJacket::getRightMirrorUV()), Adapters::toXMVECTOR(mirrorRightMask), mirrorCol, jacket.getTexID(), stage.getID());
		renderer->endBatch();
		
		basicShader->use();
		basicShader->setMatrix4("projection", Camera::getOffCenterOrthographicProjectionStatic(0, 2048, 0, 2048));
		renderer->beginBatch();
		renderer->pushQuad(Adapters::toXMVECTOR(mainLeftPos), Adapters::toXMVECTOR(mainLeftMask), DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(Adapters::toXMVECTOR(mainRightPos), Adapters::toXMVECTOR(mainRightMask), DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(Adapters::toXMVECTOR(mirrorLeftPos), Adapters::toXMVECTOR(mirrorLeftMask), DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(Adapters::toXMVECTOR(mirrorRightPos), Adapters::toXMVECTOR(mirrorRightMask), DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);

		sprite = &stage.sprites[SPR_JACKET_WINDOW];
		renderer->drawRectangle({682, 497}, {686, 686}, stage, sprite->getX1(), sprite->getX2(), sprite->getY1(), sprite->getY2(), defaultTint, 1);
		sprite = &stage.sprites[SPR_MIRROR_JACKET_WINDOW];
		renderer->drawRectangle({699, 958}, {651, 650}, stage, sprite->getX1(), sprite->getX2(), sprite->getY1(), sprite->getY2(), defaultTint.scaleAlpha(0.6f), 1);
		renderer->endBatch();

		auto mainWindowPos = Engine::quadvPos(824, 824 + 400, 666, 666 + 384);
		auto mainWindowMask = Engine::quadUV(stage.sprites[SPR_JACKET_MASK], stage);
		auto mirrorWindowPos = Engine::quadvPos(834, 834 + 386, 1120, 1120 + 336);
		auto mirrorWindowMask = Engine::quadUV(stage.sprites[SPR_MIRROR_JACKET_MASK], stage);
		maskShader->use();
		renderer->beginBatch();
		renderer->pushQuadMasked(Adapters::toXMVECTOR(mainWindowPos), Adapters::toXMVECTOR(DefaultJacket::getCenterUV()), Adapters::toXMVECTOR(mainWindowMask), DirectX::XMVectorSet(1, 1, 1, 0.8), jacket.getTexID(), stage.getID());
		renderer->pushQuadMasked(Adapters::toXMVECTOR(mirrorWindowPos), Adapters::toXMVECTOR(DefaultJacket::getMirrorCenterUV()), Adapters::toXMVECTOR(mirrorWindowMask), DirectX::XMVectorSet(1, 1, 1, 0.5), jacket.getTexID(), stage.getID());
		renderer->endBatch();

		basicShader->use();
		renderer->beginBatch();
		renderer->pushQuad(Adapters::toXMVECTOR(mainWindowPos), Adapters::toXMVECTOR(mainWindowMask), DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		renderer->pushQuad(Adapters::toXMVECTOR(mirrorWindowPos), Adapters::toXMVECTOR(mirrorWindowMask), DirectX::XMMatrixIdentity(), defCol, stage.getID(), 0);
		sprite = &stage.sprites[SPR_SEKAI_FLOOR];
		renderer->drawRectangle({0, 1251}, {sprite->getWidth(), sprite->getHeight()}, stage, sprite->getX1(), sprite->getX2(), sprite->getY1(), sprite->getY2(), defaultTint.scaleAlpha(0.8f), 1);
		renderer->endBatch();
	}

	bool ScorePreviewBackground::shouldUpdate(const Jacket& jacket) const
	{
		return !init || backgroundFile != config.backgroundImage || jacketFile != jacket.getFilename();
	}

	void ScorePreviewBackground::draw(Renderer *renderer, float scrWidth, float scrHeight) const
	{
		float bgScrWidth = frameBuffer.getWidth(), bgScrHeight = frameBuffer.getHeight(), targetWidth, targetHeight;
		if (!backgroundFile.empty())
		{
			Utils::fillRect(scrWidth, scrHeight, bgScrWidth / bgScrHeight, bgScrWidth, bgScrHeight);
			targetWidth = config.pvLockAspectRatio ? Engine::STAGE_TARGET_WIDTH : scrWidth;
			targetHeight = config.pvLockAspectRatio ? Engine::STAGE_TARGET_HEIGHT : scrHeight;
		}
		else
		{
			bgScrWidth = Engine::BACKGROUND_SIZE;
			bgScrHeight = Engine::BACKGROUND_SIZE;
			targetWidth = Engine::STAGE_TARGET_WIDTH;
			targetHeight = Engine::STAGE_TARGET_HEIGHT;
		}
		
		const float bgWidth = bgScrWidth / (targetWidth * Engine::STAGE_WIDTH_RATIO);
		const float bgLeft = -bgWidth / 2;
		const float bgHeight = bgScrHeight / (targetHeight * Engine::STAGE_HEIGHT_RATIO);
		const float centerY = 0.5f / Engine::STAGE_HEIGHT_RATIO + Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float bgTop = centerY + -bgHeight / 2;
		auto vPos = Engine::quadvPos(bgLeft, bgLeft + bgWidth, bgTop, bgTop + bgHeight);
		auto uv = Engine::quadvPos(0, 1, 0, 1);
	renderer->pushQuad(Adapters::toXMVECTOR(vPos), Adapters::toXMVECTOR(uv), DirectX::XMMatrixIdentity(), DirectX::XMVectorSet(brightness, brightness, brightness, 1), frameBuffer.getTexture(), -10);
}

std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getLeftUV()
{
	return {{	
		{  303.8 / 740, 504.8 / 740, 0, 0 },
		{  317.5 / 740, 297.7 / 740, 0, 0 },
		{    5.5 / 740, 278.3 / 740, 0, 0 },
		{     -8 / 740, 497.4 / 740, 0, 0 }
	}};
}

std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getRightUV()
{
	return {{
		{ 749.5 / 740, 377.7 / 740, 0, 0 },
		{ 738.2 / 740, 188.1 / 740, 0, 0 },
		{ 415.0 / 740, 171.4 / 740, 0, 0 },
		{ 432.1 / 740, 363.9 / 740, 0, 0 }
	}};
}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getLeftMirrorUV()
	{
		return {{
			{ 292.761414 / 740, 247.401382 / 740, 0, 0 },
			{ 310.765869 / 740, 491.944763 / 740, 0, 0 },
			{   6.892246 / 740, 498.470642 / 740, 0, 0 },
			{  -6.246704 / 740, 258.264862 / 740, 0, 0 }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getRightMirrorUV()
	{
		return {{
			{ 733.444458 / 740, 183.954681 / 740, 0, 0 },	
			{ 743.541321 / 740, 355.960449 / 740, 0, 0 },
			{ 418.899414 / 740, 332.759491 / 740, 0, 0 },
			{ 410.746246 / 740, 155.907684 / 740, 0, 0 },
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getCenterUV()
	{
		return {{
			{ 755.541687 / 740, 744.057861 / 740, 0, 0 },
			{ 739.961182 / 740,  -1.859504 / 740, 0, 0 },
			{   0.043696 / 740,  -1.859504 / 740, 0, 0 },
			{ -17.484388 / 740, 744.057861 / 740, 0, 0 }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getMirrorCenterUV()
	{
		return {{
			{ 747.697083 / 740,   2.164453 / 740, 0, 0 },
			{ 743.909424 / 740, 731.297241 / 740, 0, 0 },
			{  -1.864066 / 740, 731.297241 / 740, 0, 0 },
			{   3.837242 / 740,   2.164453 / 740, 0, 0 }
		}};
	}

	ScorePreviewWindow::ScorePreviewWindow() : previewBuffer{ 1920, 1080 }, background(), scaledAspectRatio(1)
	{
		noteEffectsCamera.setFov(50.f);
		noteEffectsCamera.setRotation(-90.f, 27.1f);
		noteEffectsCamera.setPosition({ 0, 5.32f, -5.86f, 0 });
		noteEffectsCamera.positionCamNormal();
	}

	ScorePreviewWindow::~ScorePreviewWindow()
	{
	}

	void ScorePreviewWindow::update(ScoreContext& context, Renderer* renderer)
	{
		bool isWindowActive = !ImGui::IsWindowDocked() || ImGui::GetCurrentWindow()->TabId == ImGui::GetWindowDockNode()->SelectedTabId;
		if (!isWindowActive)
			return;
			
		if (context.scorePreviewDrawData.noteSpeed != config.pvNoteSpeed)
			context.scorePreviewDrawData.calculateDrawData(context.score);
			
		ImVec2 size = ImGui::GetContentRegionAvail() - ImVec2{ this->getScrollbarWidth(), 0 };
		ImVec2 position = ImGui::GetCursorScreenPos();
		ImRect boundaries = ImRect(position, position + size);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(boundaries.Min, boundaries.Max, 0xff202020);
		
		if (config.drawBackground && background.shouldUpdate(context.workingData.jacket))
			background.update(renderer, context.workingData.jacket);

		if (!context.scorePreviewDrawData.effectView.isInitialized())
			context.scorePreviewDrawData.effectView.init();

		if (playbackState.isPlaying)
		{
			if (!playbackState.wasLastFramePlaying)
				context.scorePreviewDrawData.effectView.reset();

			context.scorePreviewDrawData.effectView.update(context);
		}

		static int shaderId = ResourceManager::getShader("basic2d");
		static int pteShaderId = ResourceManager::getShader("particles");
		if (shaderId == -1 || pteShaderId == -1)
			return;

		Shader* shader = ResourceManager::shaders[shaderId];
		Shader* pteShader = ResourceManager::shaders[pteShaderId];
		shader->use();

		float width  = size.x, height = size.y;
		float scaledWidth = (config.pvLockAspectRatio ? Engine::STAGE_TARGET_WIDTH : size.x) * Engine::STAGE_WIDTH_RATIO;
		float scaledHeight = (config.pvLockAspectRatio ? Engine::STAGE_TARGET_HEIGHT : size.y) * Engine::STAGE_HEIGHT_RATIO;
		float scrTop  = (config.pvLockAspectRatio ? Engine::STAGE_TARGET_HEIGHT : size.y) * Engine::STAGE_TOP_RATIO;
		if (config.pvLockAspectRatio) Utils::fillRect(Engine::STAGE_TARGET_WIDTH, Engine::STAGE_TARGET_HEIGHT, size.x / size.y, width, height);
		
		float aspectRatio = width / height;
		scaledAspectRatio = scaledWidth / scaledHeight;

		auto view = DirectX::XMMatrixScaling(scaledWidth, scaledHeight, 1.f) * DirectX::XMMatrixTranslation(0.f, -scrTop, 0.f);
		auto projection = Camera::getOffCenterOrthographicProjectionStatic(-width / 2, width / 2, height / 2, -height / 2);
		auto viewProjection = view * projection;

		const auto pView = noteEffectsCamera.getViewMatrix();
		auto pProjection = noteEffectsCamera.getProjectionMatrix(aspectRatio, 0.3f, 1000.f);
		float projectionScale = std::min(aspectRatio / EFFECTS_TARGET_ASPECT, 1.f);
		pProjection = DirectX::XMMatrixScaling(projectionScale, projectionScale, 1.f) * pProjection;
		
		shader->setMatrix4("projection", viewProjection);
		float currentTime = context.getTimeAtCurrentTick();

		if (previewBuffer.getWidth() != size.x || previewBuffer.getHeight() != size.y)
			previewBuffer.resize(size.x, size.y);
		previewBuffer.bind();
		previewBuffer.clear();

		renderer->beginBatch();
		if (config.drawBackground)
		{
			background.setBrightness(config.pvBackgroundBrightness);
			background.draw(renderer, width, height);
		}
		drawStage(renderer);
		renderer->endBatch();

		context.scorePreviewDrawData.effectView.updateEffects(context, noteEffectsCamera, currentTime);

		shader->use();
		shader->setMatrix4("projection", viewProjection);
		renderer->beginBatch();
		drawLines(context, renderer);
		drawHoldCurves(context, renderer);
		if (config.pvStageCover != 0) {
			drawStageCoverMask(renderer);
			renderer->endBatchWithDepthTest(GL_LEQUAL);
		}
		else
			renderer->endBatch();

		pteShader->use();
		pteShader->setMatrix4("projection", pProjection);
		pteShader->setMatrix4("view", pView);
		renderer->beginBatch();
		context.scorePreviewDrawData.effectView.drawUnderNoteEffects(renderer, currentTime);
		renderer->endBatchWithBlending(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		shader->use();
		shader->setMatrix4("projection", viewProjection);
		renderer->beginBatch();

		drawHoldTicks(context, renderer);
		drawNotes(context, renderer);
		if (config.pvStageCover != 0) {
			drawStageCoverMask(renderer);
			drawStageCover(renderer);
			drawStageCoverDecoration(renderer);
			renderer->endBatchWithDepthTest(GL_LEQUAL);
		}
		else
			renderer->endBatch();

		pteShader->use();
		pteShader->setMatrix4("projection", pProjection);
		pteShader->setMatrix4("view", pView);
		renderer->beginBatch();
		context.scorePreviewDrawData.effectView.drawEffects(renderer, currentTime);
		renderer->endBatchWithBlending(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		previewBuffer.unblind();
		drawList->AddImage((ImTextureID)(size_t)previewBuffer.getTexture(), position, position + size, {0, 1}, {1, 0});
	}

	void ScorePreviewWindow::updateUI(ScoreEditorTimeline& timeline, ScoreContext& context)
	{
		lastFrameFullWindow = fullWindow;

		updateToolbar(timeline, context);
		ImGuiIO io = ImGui::GetIO();
		float mouseWheel = io.MouseWheel * 1;
		if (!timeline.isPlaying() && ImGui::IsWindowHovered() && mouseWheel != 0)
		{
			context.currentTick += std::max(mouseWheel * TICKS_PER_BEAT / 2, (float)-context.currentTick);
		}
		updateScrollbar(timeline, context);

		playbackState.wasLastFramePlaying = playbackState.isPlaying;
		playbackState.isPlaying = timeline.isPlaying();

		if (isFullWindow())
		{
			if (ImGui::BeginPopupContextWindow("preview_context_menu", ImGuiPopupFlags_NoOpenOverExistingPopup | ImGuiPopupFlags_MouseButtonDefault_))
			{
				bool _fullWindow = fullWindow;
				if (ImGui::MenuItem(getString("fullscreen_preview"), ToShortcutString(config.input.togglePreviewFullWindow), &_fullWindow))
					setFullWindow(_fullWindow);

				ImGui::MenuItem(getString("preview_draw_toolbar"), NULL, &config.pvDrawToolbar);
				ImGui::MenuItem(getString("return_to_last_tick"), NULL, &config.returnToLastSelectedTickOnPause);
				ImGui::EndPopup();
			}
		}
	}

	void ScorePreviewWindow::setFullWindow(bool _fullWindow)
	{
		fullWindow = _fullWindow;
	}

	const Texture &ScorePreviewWindow::getNoteTexture()
	{
		// Use MMW-compatible note skins for preview
		return ResourceManager::textures[noteSkins.getItemIndex(NoteSkinItem::Notes)];
	}

	void ScorePreviewWindow::drawStage(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		constexpr float stageLeft  = -stageWidth / 2;
		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		constexpr float stageHeight = Engine::STAGE_TEX_HEIGHT / Engine::STAGE_LANE_HEIGHT;
		renderer->drawRectangle({stageLeft, stageTop}, {stageWidth, stageHeight}, stage, stageSprite.getX1(), stageSprite.getX2(), stageSprite.getY1(), stageSprite.getY2(), defaultTint.scaleAlpha(config.pvStageOpacity), -1);
	}

	void ScorePreviewWindow::drawStageCoverMask(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		constexpr float stageLeft = -stageWidth / 2, stageRight = stageWidth / 2;

		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = config.pvStageCover * (1 - stageTop);

		static auto model = DirectX::XMMatrixTranslation(0, 0, 1);
		Adapters::drawQuad(renderer,
			Engine::quadvPos(stageLeft, stageRight, stageTop + stageHeight, 0), model, stage,
			0, 0, 1, 1, defaultTint.scaleAlpha(0), 0
		);
	}

	void ScorePreviewWindow::drawStageCover(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1)
			return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites))
			return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		const float stageLeft = -stageWidth / 2, stageRight = stageWidth / 2;

		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = config.pvStageCover * (1 - stageTop);
		const float spriteHeight = config.pvStageCover * (Engine::STAGE_LANE_HEIGHT - Engine::STAGE_LANE_TOP);

		static auto model = DirectX::XMMatrixTranslation(0, 0, 1);
		Adapters::drawQuad(renderer,
			Engine::quadvPos(stageLeft, stageRight, stageTop + stageHeight, stageTop), model, stage,
			stageSprite.getX1(), stageSprite.getX2(), stageSprite.getY1(), stageSprite.getY1() + spriteHeight,
			Color{0.f, 0.f, 0.f, config.pvStageOpacity}, 0
		);
	}

	void MikuMikuWorld::ScorePreviewWindow::drawStageCoverDecoration(Renderer *renderer)
	{
		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const Texture& noteTex = getNoteTexture();
		size_t sprIndex = SPR_SIMULTANEOUS_CONNECTION;
		size_t transIndex = static_cast<size_t>(SpriteType::SimultaneousLine);
		if (!isArrayIndexInBounds(sprIndex, noteTex.sprites)) return;
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms)) return;
		const SpriteTransform& lineTransform = ResourceManager::spriteTransforms[transIndex];
		const Sprite& sprite = noteTex.sprites[sprIndex];
		float x = 0.12 * (1 - config.pvStageCover);
		auto vPos = lineTransform.apply(Engine::perspectiveQuadvPos(-6 - x, 6 + x, 1. + Engine::getNoteHeight(), 1. - Engine::getNoteHeight()));
		float y = stageTop + config.pvStageCover * (1 - stageTop);
		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		Adapters::drawQuad(renderer, vPos, model, noteTex,
			sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(),
			defaultTint.scaleAlpha(config.pvStageOpacity), -1
		);
	}

	void ScorePreviewWindow::drawNotes(const ScoreContext& context, Renderer *renderer)
	{
		double current_tm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const auto& drawData = context.scorePreviewDrawData;
		
		// MMWCC Performance: Cache scaled time per layer to avoid repeated calculation
		// Most charts use layers 0-3, so this is a small map
		std::unordered_map<int, double> layerScaledTimeCache;
		auto getScaledTime = [&](int layer) -> double {
			auto it = layerScaledTimeCache.find(layer);
			if (it != layerScaledTimeCache.end())
				return it->second;
			double time = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, layer);
			layerScaledTimeCache[layer] = time;
			return time;
		};
	
		for (auto& note : drawData.drawingNotes)
		{
			// Get cached scaled time for this note's layer
			double noteScaledTm = getScaledTime(note.layer);
			
			if (noteScaledTm < note.visualTime.min || noteScaledTm > note.visualTime.max)
				continue;

			auto it = context.score.notes.find(note.refID);
			if (it == context.score.notes.end())
				continue;

			const Note& noteData = it->second;
			double y = Engine::approach(note.visualTime.min, note.visualTime.max, noteScaledTm);
			float l = Engine::laneToLeft(noteData.lane), r = Engine::laneToLeft(noteData.lane) + noteData.width;
			
			// MMWCC: Handle damage notes
			if (noteData.getType() == NoteType::Damage)
			{
				drawDamageNote(renderer, noteData, y);
				continue;
			}
			
			drawNoteBase(renderer, noteData, l, r, y);
			if (noteData.friction)
				drawTraceDiamond(renderer, noteData, l, r, y);
			if (noteData.isFlick()) 
				drawFlickArrow(renderer, noteData, y, current_tm);
		}
	}

	void ScorePreviewWindow::drawLines(const ScoreContext& context, Renderer* renderer)
	{
		if (!config.pvSimultaneousLine)
			return;

		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;
		double scaled_tm = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, 0);
		const auto& drawData = context.scorePreviewDrawData.drawingLines;
		const Texture& texture = getNoteTexture();

		size_t sprIndex = SPR_SIMULTANEOUS_CONNECTION;
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& sprite = texture.sprites[sprIndex];
		size_t transIndex = static_cast<size_t>(SpriteType::SimultaneousLine);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& lineTransform = ResourceManager::spriteTransforms[transIndex];
		const float noteTop = 1. + Engine::getNoteHeight(), noteBottom = 1. - Engine::getNoteHeight();

		for (auto& line : drawData)
		{
			if (scaled_tm < line.visualTime.min || scaled_tm > line.visualTime.max)
				continue;
			float noteLeft = line.xPos.min, noteRight = line.xPos.max;
			if (config.pvMirrorScore)
				std::swap(noteLeft *= -1, noteRight *= -1);
			float y = Engine::approach(line.visualTime.min, line.visualTime.max, scaled_tm);
			auto vPos = lineTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteRight, noteTop, noteBottom));
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);
			Adapters::drawQuad(renderer, vPos, model, texture, sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(), defaultTint, Engine::getZIndex(SpriteLayer::UNDER_NOTE_EFFECT, 0, y));
		}
	}

	void ScorePreviewWindow::drawHoldTicks(const ScoreContext &context, Renderer *renderer)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;
		const float notesHeight = Engine::getNoteHeight() * 1.3f;
		const float w = notesHeight / scaledAspectRatio;
		const float noteTop = 1. + notesHeight, noteBottom = 1. - notesHeight;
		const Texture& texture = getNoteTexture();

		// MMWCC Performance: Cache scaled time per layer
		std::unordered_map<int, double> layerScaledTimeCache;
		auto getScaledTime = [&](int layer) -> double {
			auto it = layerScaledTimeCache.find(layer);
			if (it != layerScaledTimeCache.end())
				return it->second;
			double time = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, layer);
			layerScaledTimeCache[layer] = time;
			return time;
		};

		for (auto& tick : context.scorePreviewDrawData.drawingHoldTicks)
		{
			double scaled_tm = getScaledTime(tick.layer);
			if (scaled_tm < tick.visualTime.min || scaled_tm > tick.visualTime.max)
				continue;

			auto it = context.score.notes.find(tick.refID);
			if (it == context.score.notes.end())
				continue;

			int sprIndex = getNoteSpriteIndex(it->second);
			if (!isArrayIndexInBounds(sprIndex, texture.sprites))
				continue;
			const Sprite& sprite = texture.sprites[sprIndex];
			size_t transIndex = static_cast<size_t>(SpriteType::HoldTick);
			if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
				return;
			const SpriteTransform& transform = ResourceManager::spriteTransforms[transIndex];
			float y = Engine::approach(tick.visualTime.min, tick.visualTime.max, scaled_tm);
			const float tickCenter = tick.center * (config.pvMirrorScore ? -1 : 1);
			
			auto vPos = transform.apply(Engine::quadvPos(
				tickCenter - w, tickCenter + w,
				noteTop, noteBottom
			));
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);
			int zIndex = Engine::getZIndex(SpriteLayer::DIAMOND, tickCenter, y);
			Adapters::drawQuad(renderer, vPos, model, texture, sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(), defaultTint, zIndex);
		}
	}

	void ScorePreviewWindow::drawHoldCurves(const ScoreContext& context, Renderer* renderer)
	{
		const float total_tm = accumulateDuration(context.scorePreviewDrawData.maxTicks, TICKS_PER_BEAT, context.score.tempoChanges);
		const double current_tm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const float noteDuration = Engine::getNoteDuration(config.pvNoteSpeed);
		const float mirror = config.pvMirrorScore ? -1 : 1;
		const auto& drawData = context.scorePreviewDrawData;

		// MMWCC Performance: Cache scaled time per layer
		std::unordered_map<int, double> layerScaledTimeCache;
		auto getScaledTime = [&](int layer) -> double {
			auto it = layerScaledTimeCache.find(layer);
			if (it != layerScaledTimeCache.end())
				return it->second;
			double time = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, layer);
			layerScaledTimeCache[layer] = time;
			return time;
		};

		for (auto& segment : drawData.drawingHoldSegments)
		{
			// Use the start layer for current scaled time (cached)
			const double current_stm = getScaledTime(segment.startLayer);
			const double visible_stm = current_stm + noteDuration;
			
			if ((std::min(segment.headTime, segment.tailTime) > visible_stm && segment.startTime > current_tm) || current_tm >= segment.endTime)
				continue;

			auto endIt = context.score.notes.find(segment.endID);
			auto startIt = context.score.notes.find(segment.holdStartID);
			if (endIt == context.score.notes.end() || startIt == context.score.notes.end())
				continue;

			const Note& holdEnd = endIt->second;
			const Note& holdStart = startIt->second;
			float holdStartCenter = Engine::getNoteCenter(holdStart) * mirror;
			bool isHoldActivated = current_tm >= segment.activeTime;
			bool isSegmentActivated = current_tm >= segment.startTime;

			// Use guideColors texture for guides (has 8 color variants), or hold texture for holds
			int textureID;
			int sprIndex;
			if (segment.isGuide)
			{
				// MMWCC: Use guideColors texture which has 8 sprites for the 8 guide colors
				textureID = noteTextures.guideColors;
				sprIndex = static_cast<int>(segment.guideColor);
			}
			else
			{
				textureID = noteSkins.getItemIndex(NoteSkinItem::LongNote);
				sprIndex = holdStart.critical ? 3 : 1;
			}
			
			if (textureID == -1)
				continue;
			const Texture& texture = ResourceManager::textures[textureID];
			
			if (!isArrayIndexInBounds(sprIndex, texture.sprites))
				continue;
			const Sprite& segmentSprite = texture.sprites[sprIndex];

			double segmentHead_stm = std::min(segment.headTime, segment.tailTime);
			double segmentTail_stm = std::max(segment.headTime, segment.tailTime);
			double segmentStart_stm = std::max(segmentHead_stm, current_stm);
			double segmentEnd_stm = std::min(segmentTail_stm, visible_stm);
			double segmentStartProgress, segmentEndProgress, holdStartProgress, holdEndProgress;

			if (!isSegmentActivated)
			{
				segmentStartProgress = 0;
				segmentEndProgress = unlerpD(segmentHead_stm, segmentTail_stm, segmentEnd_stm);
			}
			else
			{
				segmentStartProgress = unlerpD(segment.startTime, segment.endTime, current_tm);
				segmentEndProgress = lerpD(segmentStartProgress, 1.0, unlerpD(current_stm, segmentTail_stm, segmentEnd_stm));
			}

			const int steps = (segment.ease == EaseType::Linear ? 10 : 15)
				+ static_cast<int>(std::log(std::max((segmentEnd_stm - segmentStart_stm) / noteDuration, 4.5399e-5)) + 0.5);
			const auto ease = getEaseFunction(segment.ease);
			float startLeft = segment.headLeft;
			float startRight = segment.headRight;
			float endLeft = segment.tailLeft;
			float endRight = segment.tailRight;

			if (isSegmentActivated)
			{
				auto holdIt = context.score.holdNotes.find(holdStart.ID);
				if (holdIt != context.score.holdNotes.end() && holdIt->second.startType == HoldNoteType::Normal)
				{
					float l = ease(startLeft, endLeft, segmentStartProgress), r = ease(startRight, endRight, segmentStartProgress);
					drawNoteBase(renderer, holdStart, l, r, 1, segment.activeTime / total_tm);
					if (holdStart.friction)
						drawTraceDiamond(renderer, holdStart, l, r, 1);
				}
			}

			if (config.pvMirrorScore)
			{
				std::swap(startLeft *= -1, startRight *= -1);
				std::swap(endLeft *= -1, endRight *= -1);
			}

			if (segment.isGuide)
			{
				auto holdIt = context.score.holdNotes.find(holdStart.ID);
				if (holdIt != context.score.holdNotes.end())
				{
					const HoldNote& hold = holdIt->second;
					double totalJoints = 1 + hold.steps.size();
					double headProgress = segment.tailStepIndex / totalJoints;
					double tailProgress = (segment.tailStepIndex + 1) / totalJoints;

					double holdStart_stm = accumulateScaledDuration(holdStart.tick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, holdStart.layer);
					double holdEnd_stm = accumulateScaledDuration(holdEnd.tick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, holdEnd.layer);
					
					if (!isSegmentActivated)
					{
						holdStartProgress = headProgress;
						holdEndProgress = lerpD(headProgress, tailProgress, unlerpD(segmentHead_stm, segmentTail_stm, segmentEnd_stm));
					}
					else
					{
						holdStartProgress = lerpD(headProgress, tailProgress, unlerp(segment.startTime, segment.endTime, current_tm));
						holdEndProgress = lerpD(holdStartProgress, tailProgress, unlerpD(current_stm, segment.tailTime, segmentEnd_stm));
					}
				}
			}

			double from_percentage = 0;
			double stepStart_stm = segmentStart_stm;
			double stepTop = Engine::approach(stepStart_stm - noteDuration, stepStart_stm, current_stm);
			double stepStartProgress = segmentStartProgress;

			auto model = DirectX::XMMatrixIdentity();
			float alpha = segment.isGuide ? config.pvGuideAlpha : config.pvHoldAlpha;
			int zIndex = Engine::getZIndex(segment.isGuide ? SpriteLayer::GUIDE_PATH : SpriteLayer::HOLD_PATH, holdStartCenter, segment.activeTime / total_tm);

			for (int i = 0; i < steps; i++)
			{
				double to_percentage = double(i + 1) / steps;
				double stepEnd_stm = lerpD(segmentStart_stm, segmentEnd_stm, to_percentage);
				double stepBottom = Engine::approach(stepEnd_stm - noteDuration, stepEnd_stm, current_stm);
				double stepEndProgress = lerpD(segmentStartProgress, segmentEndProgress, to_percentage);

				float stepStartLeft = ease(startLeft, endLeft, stepStartProgress);
				float stepEndLeft = ease(startLeft, endLeft, stepEndProgress);
				float stepStartRight = ease(startRight, endRight, stepStartProgress);
				float stepEndRight = ease(startRight, endRight, stepEndProgress);

				auto vPos = Engine::perspectiveQuadvPos(stepStartLeft, stepEndLeft, stepStartRight, stepEndRight, stepTop, stepBottom);

				float spr_x1, spr_x2, spr_y1, spr_y2;
				if (segment.isGuide)
				{
					// guideColors texture has 1-pixel-tall sprites, use coordinates directly
					// No Y cutoff manipulation - the texture is a simple horizontal gradient per color
					spr_x1 = segmentSprite.getX1();
					spr_x2 = segmentSprite.getX2();
					spr_y1 = segmentSprite.getY1();
					spr_y2 = segmentSprite.getY2();
				}
				else
				{
					spr_x1 = segmentSprite.getX1() + HOLD_XCUTOFF;
					spr_x2 = segmentSprite.getX2() - HOLD_XCUTOFF;
					spr_y1 = segmentSprite.getY1();
					spr_y2 = segmentSprite.getY2();
				}

				// Calculate alpha for guide fade in/out
				float segmentAlpha = alpha;
				if (segment.isGuide && segment.fadeType != FadeType::None)
				{
					float fadeProgress = lerpD(segmentStartProgress, segmentEndProgress, from_percentage);
					if (segment.fadeType == FadeType::In)
						segmentAlpha *= fadeProgress; // 0 -> 1
					else if (segment.fadeType == FadeType::Out)
						segmentAlpha *= (1.0f - fadeProgress); // 1 -> 0
				}

				// Hold animation pulsing effect - only for holds, not guides
				if (!segment.isGuide && config.pvHoldAnimation && isHoldActivated && isArrayIndexInBounds(sprIndex - 1, texture.sprites))
				{
					const Sprite& activeSprite = texture.sprites[sprIndex - 1];
					const int norm2ActiveOffset = activeSprite.getY1() - segmentSprite.getY1();
					double delta_tm = current_tm - segment.activeTime;
					float normalAlpha = (std::cos(delta_tm * NUM_PI * 2) + 2) / 3.;

					Adapters::drawQuad(renderer, vPos, model, texture, spr_x1, spr_x2, spr_y1, spr_y2, defaultTint.scaleAlpha(segmentAlpha * normalAlpha), zIndex);
					Adapters::drawQuad(renderer, vPos, model, texture, spr_x1, spr_x2, spr_y1 + norm2ActiveOffset, spr_y2 + norm2ActiveOffset, defaultTint.scaleAlpha(segmentAlpha * (1.f - normalAlpha)), zIndex);
				}
				else
					Adapters::drawQuad(renderer, vPos, model, texture, spr_x1, spr_x2, spr_y1, spr_y2, defaultTint.scaleAlpha(segmentAlpha), zIndex);

				from_percentage = to_percentage;
				stepStart_stm = stepEnd_stm;
				stepTop = stepBottom;
				stepStartProgress = stepEndProgress;
			}
		}
	}

	void ScorePreviewWindow::drawNoteBase(Renderer* renderer, const Note& note, float noteLeft, float noteRight, float y, float zScalar)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;

		const Texture& texture = getNoteTexture();
		const int sprIndex = getNoteSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& sprite = texture.sprites[sprIndex];

		size_t transIndex = static_cast<size_t>(SpriteType::NoteMiddle);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& mTransform = ResourceManager::spriteTransforms[transIndex];

		transIndex = static_cast<size_t>(SpriteType::NoteLeft);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& lTransform = ResourceManager::spriteTransforms[transIndex];

		transIndex = static_cast<size_t>(SpriteType::NoteRight);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& rTransform = ResourceManager::spriteTransforms[transIndex];

		const float noteHeight = Engine::getNoteHeight();
		const float noteTop = 1. - noteHeight, noteBottom = 1. + noteHeight;
		if (config.pvMirrorScore)
			std::swap(noteLeft *= -1, noteRight *= -1);
		int zIndex = Engine::getZIndex(
			!note.friction ? SpriteLayer::BASE_NOTE : SpriteLayer::TICK_NOTE,
			noteLeft + (noteRight - noteLeft) / 2.f, y * zScalar
		);

		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		std::array<DirectX::XMFLOAT4, 4> vPos;
		// Middle
		vPos = mTransform.apply(Engine::perspectiveQuadvPos(noteLeft + 0.25f, noteRight - 0.3f, noteTop, noteBottom));
		Adapters::drawQuad(renderer, vPos, model, texture,
			sprite.getX1() + NOTE_SIDE_WIDTH, sprite.getX2() - NOTE_SIDE_WIDTH, sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);

		// Left slice
		vPos = lTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteLeft + 0.25f, noteTop, noteBottom));
		Adapters::drawQuad(renderer, vPos, model, texture,
			sprite.getX1() + NOTE_SIDE_PAD, sprite.getX1() + NOTE_SIDE_WIDTH, sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);
		
		// Right slice
		vPos = rTransform.apply(Engine::perspectiveQuadvPos(noteRight - 0.3, noteRight, noteTop, noteBottom));
		Adapters::drawQuad(renderer, vPos, model, texture,
			sprite.getX2() - NOTE_SIDE_WIDTH, sprite.getX2() - NOTE_SIDE_PAD, sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);
	}

	void ScorePreviewWindow::drawTraceDiamond(Renderer *renderer, const Note &note, float noteLeft, float noteRight, float y)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;
			
		const Texture& texture = getNoteTexture();
		int frictionSprIndex = getFrictionSpriteIndex(note);
		if (!isArrayIndexInBounds(frictionSprIndex, texture.sprites))
			return;
		const Sprite& frictionSpr = texture.sprites[frictionSprIndex];
		size_t transIndex = static_cast<size_t>(SpriteType::TraceDiamond);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[transIndex];

		const float w = Engine::getNoteHeight() / scaledAspectRatio;
		const float noteTop = 1. + Engine::getNoteHeight(), noteBottom = 1. - Engine::getNoteHeight();
		if (config.pvMirrorScore)
			std::swap(noteLeft *= -1, noteRight *= -1);
		const float noteCenter = noteLeft + (noteRight - noteLeft) / 2;
		int zIndex = Engine::getZIndex(SpriteLayer::DIAMOND, noteCenter, y);

		auto vPos = transform.apply(Engine::quadvPos(
			noteCenter - w,
			noteCenter + w,
			noteTop, noteBottom)
		);
		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		
		Adapters::drawQuad(renderer, vPos, model, texture, frictionSpr.getX1(), frictionSpr.getX2(), frictionSpr.getY1(), frictionSpr.getY2(), defaultTint, zIndex);
	}

	void ScorePreviewWindow::drawFlickArrow(Renderer *renderer, const Note &note, float y, double time)
	{
		if (noteSkins.getItemIndex(NoteSkinItem::Notes) == -1)
			return;

		const Texture& texture = getNoteTexture();
		const int sprIndex = getFlickArrowSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& arrowSprite = texture.sprites[sprIndex];
		
		// MMWCC: note.width is float, need to convert to int for sprite index
		int widthInt = static_cast<int>(std::round(note.width));
		size_t flickTransformIdx = std::clamp(widthInt, 1, MAX_FLICK_SPRITES) - 1 + static_cast<int>((note.flick == FlickType::Left || note.flick == FlickType::Right) ? SpriteType::FlickArrowLeft : SpriteType::FlickArrowUp);
		if (!isArrayIndexInBounds(flickTransformIdx, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[flickTransformIdx];

		const int mirror = config.pvMirrorScore ? -1 : 1;
		const int flickDirection = mirror * (note.flick == FlickType::Left ? -1 : (note.flick == FlickType::Right ? 1 : 0));
		const float center = Engine::getNoteCenter(note) * mirror;
		const float w = std::clamp(widthInt, 0, MAX_FLICK_SPRITES) * (note.flick == FlickType::Right ? -1 : 1) * mirror / 4.f;
		
		auto vPos = transform.apply(
			Engine::quadvPos(
				center - w,
				center + w,
				1, 1 - 2 * std::abs(w) * scaledAspectRatio
			)
		);
		int zIndex = Engine::getZIndex(SpriteLayer::FLICK_ARROW, center, y);
		
		if (config.pvFlickAnimation)
		{
			double t = std::fmod(time, 0.5) / 0.5;
			const auto cubicEaseIn = [](double t) { return t * t * t; };
			auto animationVector = DirectX::XMVectorScale(DirectX::XMVectorSet(flickDirection, -2 * scaledAspectRatio, 0.f, 0.f), t);
			auto model = DirectX::XMMatrixTranslationFromVector(animationVector) * DirectX::XMMatrixScaling(y, y, 1.f);
			
			Adapters::drawQuad(renderer, vPos, model, texture,
				arrowSprite.getX1(), arrowSprite.getX2(), arrowSprite.getY1(), arrowSprite.getY2(),
				defaultTint.scaleAlpha(1 - cubicEaseIn(t)), zIndex
			);
		}
		else
		{
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);

			Adapters::drawQuad(renderer, vPos, model, texture,
				arrowSprite.getX1(), arrowSprite.getX2(), arrowSprite.getY1(), arrowSprite.getY2(),
				defaultTint, zIndex
			);
		}
	}

	void ScorePreviewWindow::drawDamageNote(Renderer* renderer, const Note& note, float y)
	{
		// MMWCC: Draw damage notes using ccNotes texture
		if (noteTextures.ccNotes == -1)
			return;

		const Texture& texture = ResourceManager::textures[noteTextures.ccNotes];
		const int sprIndex = getCcNoteSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites))
			return;
		const Sprite& sprite = texture.sprites[sprIndex];

		float noteLeft = Engine::laneToLeft(note.lane);
		float noteRight = noteLeft + note.width;
		
		size_t transIndex = static_cast<size_t>(SpriteType::NoteMiddle);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms))
			return;
		const SpriteTransform& mTransform = ResourceManager::spriteTransforms[transIndex];

		const float noteHeight = Engine::getNoteHeight();
		const float noteTop = 1. - noteHeight, noteBottom = 1. + noteHeight;
		if (config.pvMirrorScore)
			std::swap(noteLeft *= -1, noteRight *= -1);
		int zIndex = Engine::getZIndex(SpriteLayer::BASE_NOTE, noteLeft + (noteRight - noteLeft) / 2.f, y);

		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		auto vPos = mTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteRight, noteTop, noteBottom));
		Adapters::drawQuad(renderer, vPos, model, texture,
			sprite.getX1(), sprite.getX2(), sprite.getY1(), sprite.getY2(),
			defaultTint, zIndex
		);
	}

	void ScorePreviewWindow::updateToolbar(ScoreEditorTimeline &timeline, ScoreContext &context) const
	{
		static float lastHoveredTime = -1;
		constexpr float MAX_NO_HOVER_TIME = 1.5f;
		static float toolBarWidth = UI::btnNormal.x * 2;
		if (!config.pvDrawToolbar)
			return;
		ImGuiIO io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImGui::GetWindowPos() + ImVec2{
			ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 4 - toolBarWidth,
			ImGui::GetStyle().WindowPadding.y * 5
		});
		ImGui::SetNextWindowSizeConstraints({48, 0}, {120, FLT_MAX}, NULL);
		float childBgAlpha = std::clamp(easeInCubic(unlerp(MAX_NO_HOVER_TIME, 0, lastHoveredTime)), 0.25f, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.f);
		
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg, childBgAlpha));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		
		ImGui::Begin("###preview_toolbar", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_ChildWindow);
		toolBarWidth = ImGui::GetWindowWidth();
		float centeredXBtn = toolBarWidth / 2 - UI::btnNormal.x / 2;
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
			lastHoveredTime = 0;
		else
			lastHoveredTime = std::min(io.DeltaTime + lastHoveredTime, MAX_NO_HOVER_TIME);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOUBLE_UP, UI::btnNormal, true, context.currentTick < context.scorePreviewDrawData.maxTicks + TICKS_PER_BEAT))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick = timeline.roundTickDown(context.currentTick, timeline.getDivision()) + (TICKS_PER_BEAT / (timeline.getDivision() / 4));
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_UP, UI::btnNormal, true, context.currentTick < context.scorePreviewDrawData.maxTicks + TICKS_PER_BEAT))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick++;
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_STOP, UI::btnNormal, false))
			timeline.stop(context);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(timeline.isPlaying() ? ICON_FA_PAUSE : ICON_FA_PLAY, UI::btnNormal))
			timeline.setPlaying(context, !timeline.isPlaying());

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOWN, UI::btnNormal, true, context.currentTick > 0))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick--;
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOUBLE_DOWN, UI::btnNormal, true, context.currentTick > 0))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick = std::max(timeline.roundTickDown(context.currentTick, timeline.getDivision()) - (TICKS_PER_BEAT / (timeline.getDivision() / 4)), 0);
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(isFullWindow() ? ICON_FA_COMPRESS : ICON_FA_EXPAND))
			fullWindow = !isFullWindow();

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_MINUS, UI::btnNormal, false, timeline.getPlaybackSpeed() > 0.25f))
			timeline.setPlaybackSpeed(context, timeline.getPlaybackSpeed() - 0.25f);

		const float playbackStrWidth = ImGui::CalcTextSize("0000%").x;
		ImGui::SetCursorPosX(toolBarWidth / 2 - playbackStrWidth / 2);
		UI::transparentButton(IO::formatString("%.0f%%", timeline.getPlaybackSpeed() * 100).c_str(), ImVec2{playbackStrWidth, UI::btnNormal.y }, false, false);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_PLUS, UI::btnNormal, false, timeline.getPlaybackSpeed() < 1.0f))
			timeline.setPlaybackSpeed(context, timeline.getPlaybackSpeed() + 0.25f);

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		float currentTm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		double currentScaledTm = accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges, context.selectedLayer);
		int currentMeasure = accumulateMeasures(context.currentTick, TICKS_PER_BEAT, context.score.timeSignatures);
		const TimeSignature& ts = context.score.timeSignatures[findTimeSignature(currentMeasure, context.score.timeSignatures)];
		const Tempo& tempo = getTempoAt(context.currentTick, context.score.tempoChanges);
		id_t hiSpeedIdx = findHighSpeedChange(context.currentTick, context.score.hiSpeedChanges, context.selectedLayer);
		float speed = 1.0f;
		if (hiSpeedIdx != static_cast<id_t>(-1))
		{
			auto hsIt = context.score.hiSpeedChanges.find(hiSpeedIdx);
			if (hsIt != context.score.hiSpeedChanges.end())
				speed = hsIt->second.speed;
		}

		char rhythmString[256];
		snprintf(rhythmString, sizeof(rhythmString), "%02d:%02d:%02d|%.2fs|%d/%d|%g BPM|%sx",
			static_cast<int>(currentTm / 60), static_cast<int>(std::fmod(currentTm, 60.f)), static_cast<int>(std::fmod(currentTm * 100, 100.f)),
			currentScaledTm,
			ts.numerator, ts.denominator,
			tempo.bpm,
			IO::formatString("%.2f", speed).c_str()
		);
		char* str = strtok(rhythmString, "|");
		ImGui::SetCursorPosX(toolBarWidth / 2 - ImGui::CalcTextSize(str).x / 2);
		ImGui::Text(str);
		for (auto&& col : {feverColor, timeColor, tempoColor, speedColor})
		{
			str = strtok(NULL, "|");
			ImGui::SetCursorPosX(toolBarWidth / 2 - ImGui::CalcTextSize(str).x / 2);
			ImGui::TextColored(ImColor(col), str);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}

	float ScorePreviewWindow::getScrollbarWidth() const
	{
		ImGuiStyle& style = ImGui::GetStyle();
		return style.ScrollbarSize + 4;
	}

    void ScorePreviewWindow::updateScrollbar(ScoreEditorTimeline &timeline, ScoreContext &context) const
	{
		constexpr float scrollpadY = 30.f;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 contentSize = ImGui::GetWindowContentRegionMax();
		ImVec2 cursorBegPos = ImGui::GetCursorStartPos();
		ImVec2 scrollbarSize = { getScrollbarWidth(), contentSize.y - cursorBegPos.y };
		
		ImGui::SetCursorPos(cursorBegPos + ImVec2{ contentSize.x - scrollbarSize.x - style.WindowPadding.x / 2, 0 });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarBg));
		ImGui::BeginChild("###scrollbar", scrollbarSize, false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleColor();

		ImVec2 scrollContentSize = ImGui::GetContentRegionAvail();
		ImVec2 scrollMaxSize = ImGui::GetWindowContentRegionMax();
		int maxTicks = std::max(context.scorePreviewDrawData.maxTicks, 1);
		float scrollRatio = std::min(Engine::getNoteDuration(config.pvNoteSpeed) / accumulateDuration(context.scorePreviewDrawData.maxTicks, TICKS_PER_BEAT, context.score.tempoChanges), 1.f);
		float progress = 1.f - std::min(float(context.currentTick) / maxTicks, 1.f);
		float handleHeight = std::max(20.f, scrollContentSize.y * scrollRatio);
		
		bool scrollbarActive = false;
		ImGui::BeginDisabled(timeline.isPlaying());
		ImGui::SetCursorPos(ImGui::GetCursorStartPos());
		ImGui::InvisibleButton("##scroll_bg", contentSize, ImGuiButtonFlags_NoNavFocus);
		scrollbarActive |= ImGui::IsItemActive();

		ImVec2 handleSize = {style.ScrollbarSize, handleHeight};
		ImVec2 handlePos = {scrollMaxSize.x / 2 - handleSize.x / 2, lerp(0, scrollMaxSize.y - handleHeight, progress)};
		ImVec2 absHandlePos = ImGui::GetWindowPos() + handlePos;

		ImGui::SetCursorPos(handlePos);
		ImGui::InvisibleButton("##scroll_handle", handleSize);
		scrollbarActive |= ImGui::IsItemActive();

		ImGuiCol_ handleColBase = scrollbarActive ? ImGuiCol_ScrollbarGrabActive : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
		
		ImGui::RenderFrame(absHandlePos, absHandlePos + ImGui::GetItemRectSize(), ImGui::GetColorU32(handleColBase), true, 3.f);
		ImGui::EndDisabled();

		if (scrollbarActive)
		{
			float absScrollStart = ImGui::GetWindowPos().y + handleSize.y / 2;
			float absScrollEnd = ImGui::GetWindowPos().y + scrollMaxSize.y - handleSize.y / 2;
			float mouseProgress = 1.f - std::clamp(unlerp(absScrollStart, absScrollEnd, io.MousePos.y), 0.f, 1.f);
			context.currentTick = std::round(lerp(0, maxTicks, mouseProgress));	
		}
		
		ImGui::EndChild();
	}

	void ScorePreviewWindow::loadNoteEffects(Effect::EffectView& effectView)
	{
		const std::string effectsDir = Application::getAppDir() + "res\\effect\\" + std::to_string(config.pvEffectsProfile) + "\\";
		size_t effectCount = arrayLength(Effect::effectNames);

		if (!IO::File::exists(effectsDir))
			return;

		// Cleanup. We don't want all profiles and their resources loaded in memory
		ResourceManager::removeAllParticleEffects();
		int texIndex = ResourceManager::getTexture("tex_note_common_all_v2.png");
		if (texIndex > -1)
			ResourceManager::disposeTexture(texIndex);

		ResourceManager::loadTexture(effectsDir + "tex_note_common_all_v2.png");

		std::vector<std::string> failedParticleFiles;
		for (size_t i = 0; i < effectCount; i++)
		{
			const std::string filename{ effectsDir + Effect::effectNames[i] + ".json" };
			int particleId = ResourceManager::loadParticleEffect(filename);

			if (particleId == -1)
				failedParticleFiles.push_back(filename);
		}

		if (!failedParticleFiles.empty())
		{
			std::string fullErrorMessage = "Failed to load the following note effects: \n\n";
			for (const auto& error : failedParticleFiles)
				fullErrorMessage.append(error).append("\n");

			IO::messageBox(
				APP_NAME,
				fullErrorMessage,
				IO::MessageBoxButtons::Ok,
				IO::MessageBoxIcon::Warning,
				Application::windowState.windowHandle
			);
		}

		effectView.reset();
		effectView.init();
	}
}

