#include "EditorCamera.h"

#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/MainEngine.h"

namespace Editor
{
	void EditorCamera::Init(const Engine::Graphics::GraphicsEngine* a_pGraphicsEngine)
	{
		m_pGraphicsEngine = a_pGraphicsEngine;
		BuildMatrix();
	}

	void EditorCamera::Update(float a_dt)
	{
		// 無効でも行列は作っておく(有効化した瞬間に不定な行列が使われないように)
		if (!m_isEnable)
		{
			m_isControlling = false;
			BuildMatrix();
			return;
		}

		//---------------------------------------------------------------
		// 操作の開始・終了判定
		//---------------------------------------------------------------
		// 右クリックを「押し始めた場所」がシーンビュー上の時だけ操作を開始する。
		// 一度始まったらカーソルが枠外へ出ても、離すまで継続する(Unityと同じ)。
		const bool _isRightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		if (!m_isControlling)
		{
			// ギズモを操作中なら奪わない
			if (_isRightDown && m_isViewportHovered && !ImGuizmo::IsUsing())
			{
				m_isControlling = true;
			}
		}
		else if (!_isRightDown)
		{
			m_isControlling = false;
		}

		if (m_isControlling)
		{
			UpdateRotation();
			UpdateMove(a_dt);
		}

		BuildMatrix();
	}

	Math::Quaternion EditorCamera::CalcRotation() const
	{
		// Vector3 を取る CreateFromYawPitchRoll は引数の軸順が入れ替わるため、
		// スカラー版(yaw, pitch, roll)を明示的に使う
		Math::Quaternion _rot = Math::Quaternion::CreateFromYawPitchRoll(
			DirectX::XMConvertToRadians(m_yaw),
			DirectX::XMConvertToRadians(m_pitch),
			0.0f
		);
		_rot.Normalize();
		return _rot;
	}

	void EditorCamera::UpdateRotation()
	{
		const ImVec2 _delta = ImGui::GetIO().MouseDelta;
		if (_delta.x == 0.0f && _delta.y == 0.0f) return;

		m_yaw   += _delta.x * m_sensitivity;
		m_pitch += _delta.y * m_sensitivity;	// 下へ動かすと下を向く

		m_pitch = std::clamp(m_pitch, -MAX_PITCH, MAX_PITCH);

		// yaw は回し続けると際限なく増えるので畳んでおく
		if (m_yaw >  180.0f) m_yaw -= 360.0f;
		if (m_yaw < -180.0f) m_yaw += 360.0f;
	}

	void EditorCamera::UpdateMove(float a_dt)
	{
		ImGuiIO& _io = ImGui::GetIO();

		// テキスト入力中はキー入力を奪わない
		if (_io.WantTextInput) return;

		// ホイールで移動速度を調整する
		if (_io.MouseWheel != 0.0f)
		{
			m_moveSpeed *= std::pow(1.2f, _io.MouseWheel);
			m_moveSpeed = std::clamp(m_moveSpeed, 0.1f, 1000.0f);
		}

		// このエンジンは左手系でローカル +Z が前方
		const Math::Quaternion _rot = CalcRotation();
		const Math::Vector3 _forward = Math::Vector3::Transform(Math::Vector3(0.0f, 0.0f, 1.0f), _rot);
		const Math::Vector3 _right   = Math::Vector3::Transform(Math::Vector3(1.0f, 0.0f, 0.0f), _rot);

		Math::Vector3 _move = Math::Vector3::Zero();
		if (ImGui::IsKeyDown(ImGuiKey_W)) _move += _forward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) _move -= _forward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) _move += _right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) _move -= _right;

		// 上下だけはワールドのY軸基準。カメラの傾きに引きずられない方が扱いやすい
		if (ImGui::IsKeyDown(ImGuiKey_E)) _move += Math::Vector3::Up();
		if (ImGui::IsKeyDown(ImGuiKey_Q)) _move -= Math::Vector3::Up();

		if (_move.LengthSquared() < 1e-8f) return;
		_move.Normalize();

		float _speed = m_moveSpeed;
		if (_io.KeyShift) _speed *= m_boostRate;

		m_pos += _move * _speed * a_dt;
	}

	void EditorCamera::SetPose(const Math::Vector3& a_pos, float a_yawDeg, float a_pitchDeg)
	{
		m_pos = a_pos;
		m_yaw = a_yawDeg;
		m_pitch = std::clamp(a_pitchDeg, -MAX_PITCH, MAX_PITCH);

		BuildMatrix();
	}

	void EditorCamera::BuildMatrix()
	{
		// カメラのワールド行列(GraphicsEngine 側で反転してビュー行列にされる)
		m_worldMat =
			Math::Matrix::CreateFromQuaternion(CalcRotation()) *
			Math::Matrix::CreateTranslation(m_pos);

		// 射影行列。アスペクトは描画解像度から取る(ECS側のカメラと同じ作り方)
		float _aspect = 16.0f / 9.0f;
		if (m_pGraphicsEngine && m_pGraphicsEngine->GetRenderHeight() > 0)
		{
			_aspect = static_cast<float>(m_pGraphicsEngine->GetRenderWidth()) / static_cast<float>(m_pGraphicsEngine->GetRenderHeight());
		}

		m_projMat = Math::Matrix::CreatePerspectiveFieldOfView(
			DirectX::XMConvertToRadians(m_fovY),
			_aspect,
			m_nearZ,
			m_farZ
		);
	}

	void EditorCamera::DrawEditUI()
	{
		Engine::EditorField::Field("Enable", m_isEnable);
		Engine::EditorField::Tooltip("右ドラッグ中のみ操作 / WASD・EQ移動 / Shift加速 / ホイールで速度");

		Engine::EditorField::Field("Cull By Game Camera", m_isCullByGameCamera);
		Engine::EditorField::Tooltip("メッシュレットのカリングをゲームカメラで行い、このカメラから眺める。\nワイヤー表示が有効ならゲームカメラの視錐台も出る");

		Engine::EditorField::Line();

		Engine::EditorField::Field("Position", m_pos, 0.1f);
		Engine::EditorField::Field("Yaw", m_yaw, 0.5f);
		if (Engine::EditorField::Field("Pitch", m_pitch, 0.5f))
		{
			m_pitch = std::clamp(m_pitch, -MAX_PITCH, MAX_PITCH);
		}

		Engine::EditorField::Line();

		Engine::EditorField::Field("MoveSpeed", m_moveSpeed, 0.1f, 0.1f, 1000.0f);
		Engine::EditorField::Field("BoostRate", m_boostRate, 0.1f, 1.0f, 100.0f);
		Engine::EditorField::Field("Sensitivity", m_sensitivity, 0.01f, 0.01f, 5.0f);

		Engine::EditorField::Line();

		Engine::EditorField::Field("FovY", m_fovY, 0.5f, 1.0f, 179.0f);
		Engine::EditorField::Field("NearZ", m_nearZ, 0.01f, 0.001f, 100.0f);
		Engine::EditorField::Field("FarZ", m_farZ, 1.0f, 1.0f, 100000.0f);

		if (ImGui::Button("Reset"))
		{
			m_pos = { 0.0f, 3.0f, -10.0f };
			m_yaw = 0.0f;
			m_pitch = 0.0f;
			m_moveSpeed = 10.0f;
		}
	}
	Math::Ray EditorCamera::ScreenPointToRay(const Math::Vector2& a_mousePos, float a_maxDistance)
	{
		// スクリーン情報取得
		if (!m_pGraphicsEngine) return {};
		const auto* _pGraphicsEngine = m_pGraphicsEngine;

		// スクリーン座標を逆射影して、近平面と遠平面のワールド座標を取る。
		// XMVector3Unproject に相当するものは Math 側に無いので、
		// ここだけ DirectXMath へ積んで渡し、結果を Math 型で受け取る
		const DirectX::XMMATRIX _proj = Math::DX::Load(m_projMat);
		const DirectX::XMMATRIX _view = Math::DX::Load(m_worldMat.Invert());
		const DirectX::XMMATRIX _world = DirectX::XMMatrixIdentity();

		// 近平面上の位置を取得
		const Math::Vector3 _nearPos = Math::DX::StoreVector3(DirectX::XMVector3Unproject(
			DirectX::XMVectorSet(a_mousePos.x, a_mousePos.y, 0.0f, 1.0f),
			0,
			0,
			static_cast<float>(_pGraphicsEngine->GetRenderWidth()),
			static_cast<float>(_pGraphicsEngine->GetRenderHeight()),
			0.0f,
			1.0f,
			_proj,
			_view,
			_world
		));

		// 前方の座標を取得
		const Math::Vector3 _farPos = Math::DX::StoreVector3(DirectX::XMVector3Unproject(
			DirectX::XMVectorSet(a_mousePos.x, a_mousePos.y, 1.0f, 1.0f),
			0,
			0,
			static_cast<float>(_pGraphicsEngine->GetRenderWidth()),
			static_cast<float>(_pGraphicsEngine->GetRenderHeight()),
			0.0f,
			1.0f,
			_proj,
			_view,
			_world
		));

		// レイの射出方向を取得
		Math::Vector3 _dir = _farPos - _nearPos;

		Math::Ray _rayInfo = {};
		_rayInfo.origin = _nearPos;
		_rayInfo.direction = _dir;
		_rayInfo.maxDistance = a_maxDistance;

		return _rayInfo;
	}
}
