#pragma once
#include <DirectXMath.h>

namespace MikuMikuWorld
{
	class Camera
	{
	  private:
		float yaw, pitch;
		DirectX::XMVECTOR position{ 0.0f, 0.0f, -1.0f, 1.0f };
		DirectX::XMVECTOR target{ 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMVECTOR front{ 0.0f, 0.0f, 0.0f, 1.0f };
		const DirectX::XMVECTOR up{ 0.0f, 1.0f, 0.0, 1.0f };

	  public:
		Camera();

		void setPositionY(float posY);
		void positionCamNormal() {} // MMW compatibility - no-op in MMWCC
	
	// MMW compatibility methods
	void setFov(float fov) {} // No-op in MMWCC
	void setRotation(float x, float y) {} // No-op in MMWCC - takes 2 or 3 params
	void setRotation(float x, float y, float z) {} // No-op in MMWCC
	void setPosition(DirectX::XMVECTOR pos) { position = pos; }
	void setPosition(float x, float y, float z) { position = DirectX::XMVectorSet(x, y, z, 1.0f); }
	DirectX::XMMATRIX getViewMatrix() const;		DirectX::XMMATRIX getOrthographicProjection(float width, float height) const;
		DirectX::XMMATRIX getOffCenterOrthographicProjection(float left, float right, float up,
		                                                     float down) const;
	static DirectX::XMMATRIX getOffCenterOrthographicProjectionStatic(float xmin, float xmax, float ymin, float ymax);
		DirectX::XMMATRIX getPerspectiveProjection() const;
	};
}