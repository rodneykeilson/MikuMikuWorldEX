#pragma once
#include <DirectXMath.h>

namespace MikuMikuWorld
{
	class Camera
	{
	  private:
		float yaw{ 0 }, pitch{ 0 }, fov{ 50.0f };
		DirectX::XMVECTOR position{ 0.0f, 0.0f, -1.0f, 1.0f };
		DirectX::XMVECTOR target{ 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMVECTOR front{ 0.0f, 0.0f, 0.0f, 1.0f };
		const DirectX::XMVECTOR up{ 0.0f, 1.0f, 0.0, 1.0f };
		DirectX::XMMATRIX viewMatrix{ DirectX::XMMatrixIdentity() };
		DirectX::XMMATRIX inverseViewMatrix{ DirectX::XMMatrixIdentity() };

	  public:
		Camera();

		void setPositionY(float posY);
		void positionCamNormal();
	
		// MMW compatibility methods
		void setFov(float _fov) { fov = _fov; }
		void setRotation(float x, float y) { yaw = x; pitch = y; }
		void setRotation(float x, float y, float z) { yaw = x; pitch = y; }
		void setPosition(DirectX::XMVECTOR pos) { position = pos; }
		void setPosition(float x, float y, float z) { position = DirectX::XMVectorSet(x, y, z, 1.0f); }
		void rotate(float deltaX, float deltaY);
		void zoom(float delta);
		
		DirectX::XMMATRIX getViewMatrix() const;
		const DirectX::XMMATRIX& getInverseViewMatrix() const { return inverseViewMatrix; }
		DirectX::XMMATRIX getProjectionMatrix(float aspectRatio, float nearZ, float farZ) const;
		DirectX::XMMATRIX getOrthographicProjection(float width, float height) const;
		DirectX::XMMATRIX getOffCenterOrthographicProjection(float left, float right, float up, float down) const;
		static DirectX::XMMATRIX getOffCenterOrthographicProjectionStatic(float xmin, float xmax, float ymin, float ymax);
		DirectX::XMMATRIX getPerspectiveProjection() const;
	};
}