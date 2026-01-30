#include "Camera.h"
#include <cmath>
#include <algorithm>

namespace MikuMikuWorld
{
	Camera::Camera() : yaw(-90), pitch(0), fov(45.0f) {}

	void Camera::setPositionY(float posY) { position.m128_f32[1] = posY; }

	void Camera::positionCamNormal()
	{
		// Match MMW's left-handed coordinate system approach
		float rYaw = DirectX::XMConvertToRadians(yaw);
		float rPitch = DirectX::XMConvertToRadians(pitch);

		DirectX::XMVECTOR _front {
			cos(rYaw) * cos(rPitch),
			-sin(rPitch),
			-sin(rYaw) * cos(rPitch),
			1.0f
		};

		front = DirectX::XMVector3Normalize(_front);

		DirectX::XMVECTOR tgt = front;
		tgt = DirectX::XMVectorAdd(tgt, position);

		// Use left-handed LookAt like MMW
		viewMatrix = DirectX::XMMatrixLookAtLH(position, tgt, DirectX::XMVECTOR{ 0.0f, 1.0f, 0.0f, 1.0f });
		inverseViewMatrix = DirectX::XMMatrixIdentity();
		inverseViewMatrix *= DirectX::XMMatrixInverse(nullptr, viewMatrix);
		inverseViewMatrix.r[3] = DirectX::XMVECTOR{ 0, 0, 0, 1 };
	}

	void Camera::rotate(float deltaX, float deltaY)
	{
		yaw += deltaX * -0.1f;
		pitch += deltaY * 0.1f;
		pitch = std::clamp(pitch, -89.0f, 89.0f);
	}

	void Camera::zoom(float delta)
	{
		position = DirectX::XMVectorAdd(position, DirectX::XMVectorScale(front, delta));
	}

	DirectX::XMMATRIX Camera::getOrthographicProjection(float width, float height) const
	{
		return DirectX::XMMatrixOrthographicLH(width, height, 0.001f, 100);
	}

	DirectX::XMMATRIX Camera::getOffCenterOrthographicProjection(float left, float right, float up,
	                                                             float down) const
	{
		return DirectX::XMMatrixOrthographicOffCenterLH(left, right, down, up, 0.001f, 100.0f);
	}

	DirectX::XMMATRIX Camera::getViewMatrix() const
	{
		return viewMatrix;
	}

	DirectX::XMMATRIX Camera::getProjectionMatrix(float aspectRatio, float nearZ, float farZ) const
	{
		// Use left-handed projection like MMW
		return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), aspectRatio, nearZ, farZ);
	}

	DirectX::XMMATRIX Camera::getOffCenterOrthographicProjectionStatic(float xmin, float xmax, float ymin, float ymax)
	{
		return DirectX::XMMatrixOrthographicOffCenterLH(xmin, xmax, ymin, ymax, 0.001f, 100.0f);
	}

	DirectX::XMMATRIX Camera::getPerspectiveProjection() const
	{
		constexpr float defaultFov = 50.0f * 3.14159265f / 180.0f;
		return DirectX::XMMatrixPerspectiveFovLH(defaultFov, 16.0f / 9.0f, 0.001f, 100.0f);
	}
}