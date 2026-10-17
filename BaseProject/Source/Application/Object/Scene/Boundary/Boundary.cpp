#include "Boundary.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Graphics/GraphicsEngine.h"
#include "Engine/Graphics/DebugDraw/DebugDraw.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/EditorField/EditorField.h"

#include "Application/ECS/World/APPWorld.h"
#include "Application/InstanceResource/SingletonEntityResource.h"
#include "Application/InstanceResource/BoundaryResource.h"
#include "Application/Components/Input/PlayerControllTag.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"

//==========================================================================================
// Boundary
//
// 行ける範囲(軸に沿った箱)を持ち、その面を「近づいたときだけ見える壁」として描くオブジェクト。
//
// ・面は1枚ずつサブミッターへ積む(SubmitBoundary)
//     描くのは BoundaryPass。面ごとに板ポリ1枚と、画像・見え方の情報が送られる。
//
// ・見え方の基準点はプレイヤー
//     面の各点とプレイヤーの距離で濃さが決まるので、近づいた辺りだけが丸く浮かび上がる。
//     プレイヤーが居ないとき(エディターで置いているときなど)はメインカメラを基準にする。
//
// ・プレイヤーは内側から外へ出られない
//     当たり判定は付けず、範囲を BoundaryResource へ書き写すだけ。
//     押し戻すのは BoundaryClampSystem(Physics 帯の押し出しの後ろ)。
//==========================================================================================
namespace App::Object
{
	namespace
	{
		// 面の名前 : インスペクターと保存のキーに使う(EBoundaryFace の順)
		constexpr std::array<const char*, static_cast<size_t>(EBoundaryFace::Count)> FACE_NAMES =
		{
			"PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ",
		};

		// 小さすぎる値で割らないための下限
		constexpr float MIN_LENGTH = 0.01f;

		// 0〜1 に畳む(UVのずらしが際限なく大きくならないように)
		float Wrap01(float a_value)
		{
			return a_value - std::floor(a_value);
		}
	}

	//======================================================================================
	// 初期化 : 画像の読み込みを始める
	//======================================================================================
	void Boundary::Awake(Engine::GameObject::ObjectContext& a_context)
	{
		RequestTexture(a_context);
	}

	//======================================================================================
	// 初期化 : 範囲を書き写しておく
	//
	// GameObject の Update は ECS の全フェーズの後に走るので、
	// Update で初めて書くと最初のフレームの押し戻し(Physics 帯)に間に合わない
	//======================================================================================
	void Boundary::Start(Engine::GameObject::ObjectContext& a_context)
	{
		WriteBoundaryResource(a_context);
	}

	//======================================================================================
	// 解放 : 範囲を外す
	//
	// 残すと、境界を消したあとも見えない壁が残る
	//======================================================================================
	void Boundary::Release(Engine::GameObject::ObjectContext& a_context)
	{
		if (a_context.pWorld && a_context.pWorld->HasResource<InstanceResource::BoundaryResource>())
		{
			a_context.pWorld->RefResource<InstanceResource::BoundaryResource>().Clear();
		}

		BaseObject::Release(a_context);
	}

	//======================================================================================
	// 更新 : 範囲を書き写し、画像のずらしと走査線などの時間を進める
	//
	// 時間はオブジェクトが持って描画のたびに渡す。
	// パスが持つと、カメラの数だけパスが回るぶん速く進んでしまう
	//======================================================================================
	void Boundary::Update(Engine::GameObject::ObjectContext& a_context)
	{
		// インスペクターで範囲を変えたら、次のフレームの押し戻しから効く
		WriteBoundaryResource(a_context);

		m_time += a_context.dt;

		m_uvScroll.x = Wrap01(m_uvScroll.x + m_look.scrollSpeed.x * a_context.dt);
		m_uvScroll.y = Wrap01(m_uvScroll.y + m_look.scrollSpeed.y * a_context.dt);
	}

	//======================================================================================
	// 描画 : 描く面を1枚ずつ積む
	//======================================================================================
	void Boundary::Draw(Engine::GameObject::ObjectContext& a_context)
	{
		DrawBoundsMarker(a_context);

		if (!a_context.pServices || !a_context.pServices->pMainEngine) return;
		if (!m_look.texRef.IsValid()) return;

		auto* _pGE = a_context.pServices->pMainEngine->RefGraphicsEngine();
		if (!_pGE) return;

		// 基準点が無ければ、近づいているかどうかが決められないので描かない
		Math::Vector3 _revealPos = {};
		if (!TryGetRevealPos(a_context, _revealPos)) return;

		const Math::Vector2 _tileSize = {
			std::max(m_look.tileSize.x, MIN_LENGTH),
			std::max(m_look.tileSize.y, MIN_LENGTH)
		};

		for (size_t _i = 0; _i < m_isDrawFace.size(); ++_i)
		{
			if (!m_isDrawFace[_i]) continue;

			Math::Vector2 _faceSize = {};
			const Math::Matrix _faceMat = CalcFaceMatrix(static_cast<EBoundaryFace>(_i), _faceSize);

			Engine::Graphics::BoundarySubmitDesc _desc = {};
			_desc.texHandle = m_look.texRef.GetRaw();
			_desc.worldMat = _faceMat;
			_desc.revealPos = _revealPos;
			_desc.revealStartDistance = m_look.revealStartDistance;
			_desc.revealEndDistance = m_look.revealEndDistance;
			_desc.color = m_look.color;

			// 画像1枚の大きさで割った数だけ並べる(面ごとに縦横の比が違っても伸びない)
			_desc.uvTiling = { _faceSize.x / _tileSize.x, _faceSize.y / _tileSize.y };
			_desc.uvScroll = m_uvScroll;

			_desc.time = m_time;
			_desc.scanLineDensity = m_look.scanLineDensity;
			_desc.opacityScanLine = m_look.opacityScanLine;
			_desc.opacityNoise = m_look.opacityNoise;
			_desc.flickeringSpeed = m_look.flickeringSpeed;
			_desc.flickeringStrength = m_look.flickeringStrength;

			_pGE->RefDrawSubmitter()->SubmitBoundary(_desc);
		}
	}

	//======================================================================================
	// シリアライズ : 設定だけを保存する(時間とずらしは保存しない)
	//======================================================================================
	void Boundary::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		a_ar.Field("Center", m_center);
		a_ar.Field("Size", m_size);

		for (size_t _i = 0; _i < m_isDrawFace.size(); ++_i)
		{
			a_ar.Field(std::string("DrawFace") + FACE_NAMES[_i], m_isDrawFace[_i]);
		}

		a_ar.GUIDField("TextureGUID", m_look.texGUID);
		a_ar.Field("Color", m_look.color);
		a_ar.Field("TileSize", m_look.tileSize);
		a_ar.Field("ScrollSpeed", m_look.scrollSpeed);
		a_ar.Field("RevealStartDistance", m_look.revealStartDistance);
		a_ar.Field("RevealEndDistance", m_look.revealEndDistance);
		a_ar.Field("ScanLineDensity", m_look.scanLineDensity);
		a_ar.Field("OpacityScanLine", m_look.opacityScanLine);
		a_ar.Field("OpacityNoise", m_look.opacityNoise);
		a_ar.Field("FlickeringSpeed", m_look.flickeringSpeed);
		a_ar.Field("FlickeringStrength", m_look.flickeringStrength);

		// ---- ここから下は後から足したもの : 追加は必ず末尾へ ----
		a_ar.Field("WallMargin", m_wallMargin);
		a_ar.Field("IsClampPlayer", m_isClampPlayer);

		// 読み込み時は画像の読み込みを始めさせる(届くまでは描かない)
		if (a_ar.IsLoading())
		{
			RequestTexture(a_context);
		}
	}

	//======================================================================================
	// 範囲の問い合わせ
	//======================================================================================
	DirectX::BoundingBox Boundary::GetBounds() const
	{
		return DirectX::BoundingBox(m_center, m_size * 0.5f);
	}

	//======================================================================================
	// 範囲を BoundaryResource へ書き写す
	//======================================================================================
	void Boundary::WriteBoundaryResource(Engine::GameObject::ObjectContext& a_context) const
	{
		if (!a_context.pWorld) return;
		if (!a_context.pWorld->HasResource<InstanceResource::BoundaryResource>()) return;

		auto& _res = a_context.pWorld->RefResource<InstanceResource::BoundaryResource>();

		// 見た目だけの壁(背景)なら押し戻さない。途中で切り替えたときのために外しておく
		if (!m_isClampPlayer)
		{
			if (_res.isActive) _res.Clear();
			return;
		}

		_res.center = m_center;
		_res.halfSize = m_size * 0.5f;
		_res.margin = std::max(m_wallMargin, 0.0f);
		_res.isActive = true;
	}

	//======================================================================================
	// 画像を読み直す
	//======================================================================================
	void Boundary::RequestTexture(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

		m_look.texRef = (m_look.texGUID != Core::DEFAULT_GUID)
			? a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::Texture>(m_look.texGUID)
			: Engine::ResourceRef<Engine::Resource::Texture>{};
	}

	//======================================================================================
	// 近づき具合を測る基準点
	//
	// 操作しているプレイヤーが居ればその位置。
	// 居なければメインカメラ(エディターで置いているときに、カメラを寄せて見え方を確かめられる)
	//======================================================================================
	bool Boundary::TryGetRevealPos(Engine::GameObject::ObjectContext& a_context, Math::Vector3& a_outPos) const
	{
		if (!a_context.pWorld) return false;
		auto& _world = *a_context.pWorld;

		// プレイヤー
		Engine::ECS::Entity _player = Engine::ECS::Limits::INVALID_ENTITY;
		_world.ForEach<const Component::ActiveTag, const Component::PlayerControllTag>(
			[&](
				Engine::ECS::Chunk* a_pChunk,
				uint32_t a_count,
				const Component::ActiveTag* /*a_activeTagArray*/,
				const Component::PlayerControllTag* /*a_playerTagArray*/
			)
			{
				if (_player != Engine::ECS::Limits::INVALID_ENTITY || a_count == 0) return;
				_player = a_pChunk->entityData[0];
			}
		);

		// プレイヤーは親を持つとは限らないので、行列から位置を取る
		if (_player != Engine::ECS::Limits::INVALID_ENTITY &&
			_world.HasComponent<Component::WorldMatrixComponent>(_player))
		{
			if (const auto* _pWorldMat = _world.RefData<Component::WorldMatrixComponent>(_player))
			{
				a_outPos = Math::Matrix(_pWorldMat->worldMat).Translation();
				return true;
			}
		}

		// メインカメラ
		if (!_world.HasResource<InstanceResource::SingletonEntityResource>()) return false;

		const Engine::ECS::Entity _camera = _world.RefResource<InstanceResource::SingletonEntityResource>().mainCamera;
		if (!_world.IsAliveEntity(_camera)) return false;
		if (!_world.HasComponent<Component::WorldMatrixComponent>(_camera)) return false;

		const auto* _pCameraMat = _world.RefData<Component::WorldMatrixComponent>(_camera);
		if (!_pCameraMat) return false;

		a_outPos = Math::Matrix(_pCameraMat->worldMat).Translation();
		return true;
	}

	//======================================================================================
	// 1x1 の板(XY 平面・表は +Z)を面へ移す行列
	//
	// 板の表を面の外向きへ回し、面の大きさに拡大して、中心から半分の長さだけ外へ出す。
	// 描くときは裏も描くので、表裏の向きは画像の向き(UV)にしか効かない
	//======================================================================================
	Math::Matrix Boundary::CalcFaceMatrix(EBoundaryFace a_face, Math::Vector2& a_outFaceSize) const
	{
		const Math::Vector3 _size = {
			std::max(m_size.x, MIN_LENGTH),
			std::max(m_size.y, MIN_LENGTH),
			std::max(m_size.z, MIN_LENGTH)
		};
		const Math::Vector3 _half = _size * 0.5f;

		constexpr float HALF_PI = DirectX::XM_PIDIV2;
		constexpr float PI = DirectX::XM_PI;

		Math::Matrix _rot = Math::Matrix::Identity();
		Math::Vector3 _offset = {};

		switch (a_face)
		{
		case EBoundaryFace::PositiveX:
			_rot = Math::Matrix::CreateFromYawPitchRoll(HALF_PI, 0.0f, 0.0f);		// +Z → +X
			_offset = { _half.x, 0.0f, 0.0f };
			a_outFaceSize = { _size.z, _size.y };
			break;
		case EBoundaryFace::NegativeX:
			_rot = Math::Matrix::CreateFromYawPitchRoll(-HALF_PI, 0.0f, 0.0f);		// +Z → -X
			_offset = { -_half.x, 0.0f, 0.0f };
			a_outFaceSize = { _size.z, _size.y };
			break;
		case EBoundaryFace::PositiveY:
			_rot = Math::Matrix::CreateFromYawPitchRoll(0.0f, -HALF_PI, 0.0f);		// +Z → +Y
			_offset = { 0.0f, _half.y, 0.0f };
			a_outFaceSize = { _size.x, _size.z };
			break;
		case EBoundaryFace::NegativeY:
			_rot = Math::Matrix::CreateFromYawPitchRoll(0.0f, HALF_PI, 0.0f);		// +Z → -Y
			_offset = { 0.0f, -_half.y, 0.0f };
			a_outFaceSize = { _size.x, _size.z };
			break;
		case EBoundaryFace::PositiveZ:
			_offset = { 0.0f, 0.0f, _half.z };
			a_outFaceSize = { _size.x, _size.y };
			break;
		case EBoundaryFace::NegativeZ:
		default:
			_rot = Math::Matrix::CreateFromYawPitchRoll(PI, 0.0f, 0.0f);			// +Z → -Z
			_offset = { 0.0f, 0.0f, -_half.z };
			a_outFaceSize = { _size.x, _size.y };
			break;
		}

		return Math::Matrix::CreateScale(a_outFaceSize.x, a_outFaceSize.y, 1.0f)
			* _rot
			* Math::Matrix::CreateTranslation(m_center + _offset);
	}

	//======================================================================================
	// 目印 : 範囲の枠(デバッグ線の表示が有効なときだけ見える)
	//======================================================================================
	void Boundary::DrawBoundsMarker(Engine::GameObject::ObjectContext& a_context) const
	{
		if (!a_context.pServices) return;

		auto* _pDebugDraw = a_context.pServices->pDebugDraw;
		if (!_pDebugDraw) return;

		constexpr Math::Color MARKER_COLOR = { 0.0f, 1.0f, 1.0f, 1.0f };
		_pDebugDraw->DrawBox(GetBounds(), MARKER_COLOR);
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void Boundary::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::HelpText("行ける範囲の境界。プレイヤーが近づいた面だけが見えてくる(当たり判定は持たない)");
		Engine::EditorField::HelpText("描くには、レンダリングパイプラインに BoundaryPass が要ります");
		Engine::EditorField::Line();

		//---------------------------------------------------------------
		// 範囲
		//---------------------------------------------------------------
		Engine::EditorField::Header("Bounds");

		Engine::EditorField::Field("Center", m_center, 0.1f);
		if (Engine::EditorField::Field("Size", m_size, 0.1f, MIN_LENGTH, 100000.0f))
		{
			m_size.x = std::max(m_size.x, MIN_LENGTH);
			m_size.y = std::max(m_size.y, MIN_LENGTH);
			m_size.z = std::max(m_size.z, MIN_LENGTH);
		}

		Engine::EditorField::Field("Clamp Player", m_isClampPlayer);
		Engine::EditorField::Tooltip("プレイヤーを範囲の内側へ押し戻すか。切ると見た目だけの壁(背景)になる");
		Engine::EditorField::Field("Wall Margin", m_wallMargin, 0.1f, 0.0f, 1000.0f);
		Engine::EditorField::Tooltip("プレイヤーの位置(足元)を壁からこれだけ内側に留める。機体の半径くらいにすると体が面にめり込まない");

		for (size_t _i = 0; _i < m_isDrawFace.size(); ++_i)
		{
			Engine::EditorField::Field(FACE_NAMES[_i], m_isDrawFace[_i]);
		}
		Engine::EditorField::Tooltip("描く面。床(NegativeY)は地面に埋まるので既定で描かない");

		//---------------------------------------------------------------
		// 見え方
		//---------------------------------------------------------------
		Engine::EditorField::Header("Look");

		if (a_context.pServices &&
			Engine::EditorField::AssetField(*a_context.pServices, "Texture", "Texture", m_look.texGUID))
		{
			RequestTexture(a_context);
		}
		if (m_look.texGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : 境界面は描かれません)");
		}

		Engine::EditorField::ColorField("Color", m_look.color);
		Engine::EditorField::Tooltip("画像に掛ける色。a は一番近づいたときの濃さ。rgb は 1 を超えるとブルームで光る");

		Engine::EditorField::Field("Tile Size", m_look.tileSize, 0.1f, MIN_LENGTH, 100000.0f);
		Engine::EditorField::Tooltip("画像1枚の大きさ(ワールドの長さ)。面の大きさをこれで割った数だけ並べる");

		Engine::EditorField::Field("Scroll Speed", m_look.scrollSpeed, 0.01f);

		Engine::EditorField::Field("Reveal Start", m_look.revealStartDistance, 0.1f, 0.0f, 100000.0f);
		Engine::EditorField::Tooltip("プレイヤーとの距離がこれより近いと見え始める");
		Engine::EditorField::Field("Reveal End", m_look.revealEndDistance, 0.1f, 0.0f, 100000.0f);
		Engine::EditorField::Tooltip("この距離で一番濃くなる。Reveal Start 以上にすると常に見える");

		//---------------------------------------------------------------
		// 走査線・ノイズ・明滅
		//---------------------------------------------------------------
		Engine::EditorField::Header("ScanLine");

		Engine::EditorField::Field("Density", m_look.scanLineDensity, 0.1f, 0.0f, 1000.0f);
		Engine::EditorField::Tooltip("画像1枚あたりの走査線の本数");
		Engine::EditorField::Field("Opacity ScanLine", m_look.opacityScanLine, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("Opacity Noise", m_look.opacityNoise, 0.01f, 0.0f, 1.0f);
		Engine::EditorField::Field("Flickering Speed", m_look.flickeringSpeed, 0.1f, 0.0f, 1000.0f);
		Engine::EditorField::Field("Flickering Strength", m_look.flickeringStrength, 0.01f, 0.0f, 1.0f);
	}

	//======================================================================================
	// ギズモ : 中心を動かす(選んでいる間だけ呼ばれる)
	//======================================================================================
	bool Boundary::DrawGizmo(
		const Engine::GameObject::ObjectGizmoContext& a_ctx,
		Engine::GameObject::ObjectContext& /*a_context*/)
	{
		Math::Matrix _mat = Math::Matrix::CreateTranslation(m_center);

		// Ctrl を押している間だけスナップ(エンティティ用ギズモと同じ操作感)
		const float _snap = Engine::EditorField::IsCtrlDown() ? 1.0f : 0.0f;

		if (Engine::EditorField::TranslateGizmo(a_ctx.viewMat, a_ctx.projMat, _mat, _snap))
		{
			m_center = _mat.Translation();
		}

		return true;
	}
}
