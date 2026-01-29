#include "Camera.h"

namespace MikuMikuWorld
{
	Camera::Camera() {}

	void Camera::setPositionY(float posY) { position.m128_f32[1] = posY; }

	DirectX::XMMATRIX Camera::getOrthographicProjection(float width, float height) const
	{
		return DirectX::XMMatrixOrthographicRH(width, height, 0.001f, 100);
	}

	DirectX::XMMATRIX Camera::getOffCenterOrthographicProjection(float left, float right, float up,
	                                                             float down) const
	{
		return DirectX::XMMatrixOrthographicOffCenterRH(left, right, down, up, 0.001f, 100.0f);
	}

	DirectX::XMMATRIX Camera::getViewMatrix() const
	{
		return DirectX::XMMatrixLookAtRH(position, target, up);
	}

	DirectX::XMMATRIX Camera::getOffCenterOrthographicProjectionStatic(float xmin, float xmax, float ymin, float ymax)
	{
		return DirectX::XMMatrixOrthographicOffCenterRH(xmin, xmax, ymin, ymax, 0.001f, 100.0f);
	}
}