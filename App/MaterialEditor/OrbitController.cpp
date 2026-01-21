#include "OrbitController.h"

#include "ThirdParty/imgui/imgui.h"

namespace shz
{
	static inline float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }

	void MaterialEditorOrbitController::UpdateFromImGuiIO(float dt, bool hovered, bool focused)
	{
		(void)dt;

		ImGuiIO& io = ImGui::GetIO();

		// hover/focus일 때만 입력 적용
		if (!(hovered && focused))
		{
			// 휠은 hover만으로도 먹게 하고 싶으면 여기 분리 가능
			return;
		}

		const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);

		const float2 md = { io.MouseDelta.x, io.MouseDelta.y };

		// Zoom
		if (io.MouseWheel != 0.0f)
		{
			// wheel up => zoom in
			const float zoom = std::pow(1.0f - m_State.ZoomSpeed, io.MouseWheel);
			m_State.Distance *= zoom;
		}

		// 좌클릭: 오브젝트 회전
		if (lmb && !rmb)
		{
			m_State.ObjectYaw += md.x * m_State.RotateSpeed;
			m_State.ObjectPitch += md.y * m_State.RotateSpeed;
			m_State.ObjectPitch = clampf(m_State.ObjectPitch, m_State.MinPitch, m_State.MaxPitch);
		}

		// 우클릭: 카메라(오브젝트 중심) 회전
		if (rmb)
		{
			m_State.Yaw += md.x * m_State.RotateSpeed;
			m_State.Pitch += md.y * m_State.RotateSpeed;
			m_State.Pitch = clampf(m_State.Pitch, m_State.MinPitch, m_State.MaxPitch);
		}

		Clamp();
	}

	void MaterialEditorOrbitController::Clamp()
	{
		m_State.Distance = clampf(m_State.Distance, m_State.MinDistance, m_State.MaxDistance);
		m_State.Pitch = clampf(m_State.Pitch, m_State.MinPitch, m_State.MaxPitch);
		m_State.ObjectPitch = clampf(m_State.ObjectPitch, m_State.MinPitch, m_State.MaxPitch);
	}

	Matrix4x4 MaterialEditorOrbitController::ComputeViewMatrix() const
	{
		// Spherical to Cartesian around Target
		const float cy = std::cos(m_State.Yaw);
		const float sy = std::sin(m_State.Yaw);
		const float cp = std::cos(m_State.Pitch);
		const float sp = std::sin(m_State.Pitch);

		const float3 forward =
		{
			sy * cp,
			sp,
			cy * cp
		};

		const float3 eye = m_State.Target - forward * m_State.Distance;

		return LookAtRH(eye, m_State.Target, float3(0, 1, 0));
	}

	Matrix4x4 MaterialEditorOrbitController::ComputeObjectRotationMatrix() const
	{
		// Yaw then Pitch (RH)
		const float cy = std::cos(m_State.ObjectYaw);
		const float sy = std::sin(m_State.ObjectYaw);
		const float cp = std::cos(m_State.ObjectPitch);
		const float sp = std::sin(m_State.ObjectPitch);

		// R = Ry * Rx
		Matrix4x4 Ry =
		{
			cy,  0, sy, 0,
			0,   1, 0,  0,
			-sy, 0, cy, 0,
			0,   0, 0,  1
		};

		Matrix4x4 Rx =
		{
			1,  0,   0,  0,
			0,  cp, -sp, 0,
			0,  sp,  cp, 0,
			0,  0,   0,  1
		};

		return Ry * Rx;
	}

	float3 MaterialEditorOrbitController::NormalizeSafe(const float3& v, float eps)
	{
		const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
		if (len2 < eps) return float3(0, 0, 0);
		const float inv = 1.0f / std::sqrt(len2);
		return float3(v.x * inv, v.y * inv, v.z * inv);
	}

	float MaterialEditorOrbitController::Dot3(const float3& a, const float3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	float3 MaterialEditorOrbitController::Cross3(const float3& a, const float3& b)
	{
		return float3(
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x);
	}

	Matrix4x4 MaterialEditorOrbitController::LookAtRH(const float3& eye, const float3& at, const float3& up)
	{
		// RH look-at: zaxis = normalize(eye - at)
		const float3 zaxis = NormalizeSafe(eye - at);
		const float3 xaxis = NormalizeSafe(Cross3(up, zaxis));
		const float3 yaxis = Cross3(zaxis, xaxis);

		Matrix4x4 m = {};

		m._m00 = xaxis.x; m._m01 = yaxis.x; m._m02 = zaxis.x; m._m03 = 0.0f;
		m._m10 = xaxis.y; m._m11 = yaxis.y; m._m12 = zaxis.y; m._m13 = 0.0f;
		m._m20 = xaxis.z; m._m21 = yaxis.z; m._m22 = zaxis.z; m._m23 = 0.0f;
		m._m30 = -Dot3(xaxis, eye);
		m._m31 = -Dot3(yaxis, eye);
		m._m32 = -Dot3(zaxis, eye);
		m._m33 = 1.0f;

		return m;
	}
} // namespace shz
