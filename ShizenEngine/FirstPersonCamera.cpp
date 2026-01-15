#include "FirstPersonCamera.h"
#include <algorithm>
#include <cmath>

namespace shz
{
    float4x4 FirstPersonCamera::GetReferenceRotiation() const
    {
        // IMPORTANT:
        // Your Matrix4x4 uses row-vector v' = v*M, and MulVector4() dots with COLUMNS.
        // Therefore basis vectors must live in COLUMNS to map local(ref) -> world.
        return float4x4
        {
            m_ReferenceRightAxis.x, m_ReferenceUpAxis.x, m_ReferenceAheadAxis.x, 0,
            m_ReferenceRightAxis.y, m_ReferenceUpAxis.y, m_ReferenceAheadAxis.y, 0,
            m_ReferenceRightAxis.z, m_ReferenceUpAxis.z, m_ReferenceAheadAxis.z, 0,
                                 0,                   0,                      0, 1
        };
    }

    void FirstPersonCamera::SetReferenceAxes(const float3& ReferenceRightAxis,
        const float3& ReferenceUpAxis,
        bool          IsRightHanded)
    {
        m_ReferenceRightAxis = ReferenceRightAxis.Normalized();

        // Make Up orthogonal to Right
        m_ReferenceUpAxis = ReferenceUpAxis - Vector3::Dot(ReferenceUpAxis, m_ReferenceRightAxis) * m_ReferenceRightAxis;

        float UpLen = m_ReferenceUpAxis.Length();
        constexpr float Epsilon = 1e-5f;
        if (UpLen < Epsilon)
        {
            UpLen = Epsilon;
            LOG_WARNING_MESSAGE("Right and Up axes are collinear");
        }
        m_ReferenceUpAxis /= UpLen;

        // +1 for RH, -1 for LH (same as Diligent sample)
        m_fHandness = IsRightHanded ? +1.f : -1.f;

        // Ahead axis:
        // If you want LH with +Z forward, this sign convention must match your engine's axis setup.
        m_ReferenceAheadAxis = m_fHandness * Vector3::Cross(m_ReferenceRightAxis, m_ReferenceUpAxis);

        float AheadLen = m_ReferenceAheadAxis.Length();
        if (AheadLen < Epsilon)
        {
            AheadLen = Epsilon;
            LOG_WARNING_MESSAGE("Ahead axis is not well defined");
        }
        m_ReferenceAheadAxis /= AheadLen;
    }

    void FirstPersonCamera::SetRotation(float Yaw, float Pitch)
    {
        m_fYawAngle = Yaw;
        m_fPitchAngle = Pitch;
    }

    void FirstPersonCamera::SetLookAt(const float3& LookAt)
    {
        // World-space view direction
        float3 ViewDirW = LookAt - m_Pos;
        if (ViewDirW.Length() < 1e-6f)
            return;
        ViewDirW = ViewDirW.Normalized();

        // Convert world direction -> reference space:
        // RefRot maps ref->world, so inverse (transpose for ortho basis) maps world->ref.
        const float4x4 RefRot = GetReferenceRotiation();
        const float4x4 InvRefRot = RefRot.Transposed(); // assuming orthonormal

        const Vector3 ViewDirRef = InvRefRot.TransformDirection(ViewDirW);

        // For LH (+Z forward): yaw = atan2(x, z)
        m_fYawAngle = std::atan2(ViewDirRef.x, ViewDirRef.z);

        const float xzLen = std::sqrt(ViewDirRef.z * ViewDirRef.z + ViewDirRef.x * ViewDirRef.x);
        m_fPitchAngle = -std::atan2(ViewDirRef.y, xzLen);

        m_fPitchAngle = Clamp(m_fPitchAngle, -PI * 0.5f, +PI * 0.5f);
    }

    void FirstPersonCamera::SetProjAttribs(
        float32 NearClipPlane,
        float32 FarClipPlane,
        float32 AspectRatio,
        float32 FOV,
        SURFACE_TRANSFORM SrfPreTransform)
    {
        m_ProjAttribs.NearClipPlane = NearClipPlane;
        m_ProjAttribs.FarClipPlane = FarClipPlane;
        m_ProjAttribs.AspectRatio = AspectRatio;
        m_ProjAttribs.FOV = FOV;
        m_ProjAttribs.PreTransform = SrfPreTransform;

        // NOTE:
        // If you actually support surface pretransform, you should apply it here.
        // For now we keep it identical to your engine's row-vector LH projection.
        m_ProjMatrix = Matrix4x4::PerspectiveFovLH(
            m_ProjAttribs.FOV,
            m_ProjAttribs.AspectRatio,
            m_ProjAttribs.NearClipPlane,
            m_ProjAttribs.FarClipPlane);
    }

    void FirstPersonCamera::SetSpeedUpScales(float32 SpeedUpScale, float32 SuperSpeedUpScale)
    {
        m_fSpeedUpScale = SpeedUpScale;
        m_fSuperSpeedUpScale = SuperSpeedUpScale;
    }

    void FirstPersonCamera::Update(InputController& Controller, float ElapsedTime)
    {
        // -------------------------
        // 1) Movement input (local)
        // -------------------------
        float3 MoveDir(0, 0, 0);
        if (Controller.IsKeyDown(InputKeys::MoveForward))  MoveDir.z += 1.0f;
        if (Controller.IsKeyDown(InputKeys::MoveBackward)) MoveDir.z -= 1.0f;

        if (Controller.IsKeyDown(InputKeys::MoveRight))    MoveDir.x += 1.0f;
        if (Controller.IsKeyDown(InputKeys::MoveLeft))     MoveDir.x -= 1.0f;

        if (Controller.IsKeyDown(InputKeys::MoveUp))       MoveDir.y += 1.0f;
        if (Controller.IsKeyDown(InputKeys::MoveDown))     MoveDir.y -= 1.0f;

        const float len = MoveDir.Length();
        if (len > 1e-6f)
            MoveDir /= len;

        const bool SpeedUp = Controller.IsKeyDown(InputKeys::ShiftDown);
        const bool SuperSpeedUp = Controller.IsKeyDown(InputKeys::ControlDown);

        float speed = m_fMoveSpeed;
        if (SpeedUp)      speed *= m_fSpeedUpScale;
        if (SuperSpeedUp) speed *= m_fSuperSpeedUpScale;

        m_fCurrentSpeed = speed * (len > 1e-6f ? 1.0f : 0.0f);

        const float3 PosDeltaLocal = MoveDir * (speed * ElapsedTime);

        // -------------------------
        // 2) Mouse look -> yaw/pitch
        // -------------------------
        {
            const auto& mouseState = Controller.GetMouseState();

            float MouseDeltaX = 0.0f;
            float MouseDeltaY = 0.0f;

            if (m_LastMouseState.PosX >= 0 && m_LastMouseState.PosY >= 0 &&
                m_LastMouseState.ButtonFlags != MouseState::BUTTON_FLAG_NONE)
            {
                MouseDeltaX = float(mouseState.PosX - m_LastMouseState.PosX);
                MouseDeltaY = float(mouseState.PosY - m_LastMouseState.PosY);
            }
            m_LastMouseState = mouseState;

            if (mouseState.ButtonFlags & MouseState::BUTTON_FLAG_LEFT)
            {
                const float yawDelta = MouseDeltaX * m_fRotationSpeed;
                const float pitchDelta = MouseDeltaY * m_fRotationSpeed;

                // Keep Diligent-style handedness sign:
                // If this feels inverted in your engine, flip the sign here.
                m_fYawAngle += yawDelta * -m_fHandness;
                m_fPitchAngle += pitchDelta * -m_fHandness;

                m_fPitchAngle = Clamp(m_fPitchAngle, -PI * 0.5f, +PI * 0.5f);
            }
        }

        // -------------------------
        // 3) Build camera rotation (WORLD)
        //    row-vector: v_world = v_local * WorldRot
        // -------------------------
        const float4x4 RefRot = GetReferenceRotiation(); // ref(local)->world

        // Yaw about reference UP in world space
        const float4x4 YawRot = float4x4::RotationAxis(m_ReferenceUpAxis, m_fYawAngle);

        // Compute current (yawed) right axis in world:
        // local right (1,0,0) -> world via RefRot then yaw
        const Vector3 RightW = (RefRot * YawRot).TransformDirection(Vector3(1, 0, 0));

        // Pitch about current right axis (FPS pitch)
        const float4x4 PitchRot = float4x4::RotationAxis(RightW, m_fPitchAngle);

        // Final world rotation
        const float4x4 WorldRot = RefRot * YawRot * PitchRot;

        // -------------------------
        // 4) Apply movement: local delta -> world delta (w=0!)
        // -------------------------
        const Vector3 PosDeltaWorld = WorldRot.TransformDirection(PosDeltaLocal);
        m_Pos += PosDeltaWorld;

        // -------------------------
        // 5) Build View/World matrices
        // World: local -> world
        // View : world -> view
        // -------------------------
        m_WorldMatrix = WorldRot * float4x4::Translation(m_Pos);

        // For pure rotation, inverse is transpose.
        const float4x4 InvRot = WorldRot.Transposed();
        m_ViewMatrix = float4x4::Translation(-m_Pos) * InvRot;
    }

} // namespace shz
