#include "Camera.h"
#include <cmath>

namespace MikuMikuWorld
{
	Camera::Camera() : yaw(0), pitch(0), fov(50.0f) {}

	void Camera::setPositionY(float posY) { position.m128_f32[1] = posY; }

	void Camera::positionCamNormal()
	{
		// Update view matrix after position/rotation changes
		viewMatrix = DirectX::XMMatrixLookAtRH(position, target, up);
		inverseViewMatrix = DirectX::XMMatrixInverse(nullptr, viewMatrix);
	}

	void Camera::rotate(float deltaX, float deltaY)
	{
		yaw += deltaX;
		pitch += deltaY;
		// Clamp pitch to avoid gimbal lock
		if (pitch > 89.0f) pitch = 89.0f;
		if (pitch < -89.0f) pitch = -89.0f;
	}

	void Camera::zoom(float delta)
	{
		// Adjust position.z for zoom
		position.m128_f32[2] += delta;
	}

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

	DirectX::XMMATRIX Camera::getProjectionMatrix(float aspectRatio, float nearZ, float farZ) const
	{
		float fovRad = fov * 3.14159265f / 180.0f;
		return DirectX::XMMatrixPerspectiveFovRH(fovRad, aspectRatio, nearZ, farZ);
	}

	DirectX::XMMATRIX Camera::getOffCenterOrthographicProjectionStatic(float xmin, float xmax, float ymin, float ymax)
	{
		return DirectX::XMMatrixOrthographicOffCenterRH(xmin, xmax, ymin, ymax, 0.001f, 100.0f);
	}

	DirectX::XMMATRIX Camera::getPerspectiveProjection() const
	{
		constexpr float defaultFov = 50.0f * 3.14159265f / 180.0f;
		return DirectX::XMMatrixPerspectiveFovRH(defaultFov, 16.0f / 9.0f, 0.001f, 100.0f);
	}
}