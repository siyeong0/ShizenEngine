// ============================================================================
// MaterialEditorOrbitController.h
// ============================================================================

#pragma once

#include <cmath>
#include <algorithm>

#include "Primitives/BasicTypes.h"
#include "Engine/Core/Math/Math.h"

namespace shz
{
	struct OrbitCameraState final
	{
		float3 Target = { 0, 0, 0 };

		// 카메라 파라미터
		float  Distance = 3.0f;
		float  MinDistance = 0.2f;
		float  MaxDistance = 50.0f;

		// 각도(라디안)
		float  Yaw = 0.0f;    // around world up
		float  Pitch = 0.0f;  // up/down
		float  MinPitch = -1.35f;
		float  MaxPitch = +1.35f;

		// 회전 민감도
		float  RotateSpeed = 0.0125f;
		float  ZoomSpeed = 0.25f;

		// 오브젝트 회전(에디터에서 대상 오브젝트 자체를 돌리는 값)
		float  ObjectYaw = 0.0f;
		float  ObjectPitch = 0.0f;
	};

	class MaterialEditorOrbitController final
	{
	public:
		void Reset(const OrbitCameraState& s) { m_State = s; }

		OrbitCameraState& GetState() noexcept { return m_State; }
		const OrbitCameraState& GetState() const noexcept { return m_State; }

		// ImGui IO 기반 입력 처리:
		// - 좌클릭 드래그: 오브젝트 회전(ObjectYaw/Pitch)
		// - 우클릭 드래그: 카메라 회전(Yaw/Pitch)
		// - 휠: 줌
		//
		// hovered/focused일 때만 적용하도록 MaterialEditor에서 gating해줘야 함.
		void UpdateFromImGuiIO(
			float dt,
			bool hovered,
			bool focused);

		// 카메라 view matrix 산출
		Matrix4x4 ComputeViewMatrix() const;

		// 오브젝트(메인 모델) TRS에 추가로 곱할 회전 행렬 (Yaw/Pitch)
		Matrix4x4 ComputeObjectRotationMatrix() const;

		// 줌/회전 clamp
		void Clamp();

	private:
		OrbitCameraState m_State = {};

	private:
		// 네 Math에 LookAt이 없을 수 있어서 안전하게 직접 구성
		static Matrix4x4 LookAtRH(const float3& eye, const float3& at, const float3& up);
		static float3 NormalizeSafe(const float3& v, float eps = 1e-8f);
		static float  Dot3(const float3& a, const float3& b);
		static float3 Cross3(const float3& a, const float3& b);
	};
} // namespace shz
