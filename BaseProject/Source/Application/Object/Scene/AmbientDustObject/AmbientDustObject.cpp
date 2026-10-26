#include "AmbientDustObject.h"

#include "Engine/ECS/System/SystemContext.h"	// ObjectContext が運ぶサービス群
#include "Engine/MainEngine.h"
#include "Engine/Resource/Manager/ResourceManager/ResourceManager.h"
#include "Engine/Resource/Data/EffectAsset/EffectAsset.h"
#include "Engine/EditorField/EditorField.h"

#include "../../../../Engine/ECS/World/World.h"

#include "Application/InstanceResource/SingletonEntityResource.h"
#include "Application/Components/Transform/LocalTransformComponent.h"
#include "Application/Components/Transform/WorldMatrixComponent.h"
#include "Application/Components/Effect/EffectAssetComponent.h"
#include "Application/Components/Effect/EffectOverrideComponent.h"
#include "Application/Utility/EffectSpawnHelper.h"

//==========================================================================================
// AmbientDustObject
//
// 空気中のチリのエフェクトを1つ出し続け、その位置を毎フレームカメラへ寄せるオブジェクト。
//
// ・環境光・平行光・フォグ・空はここには無い
//     シーンそのものの性質なので、シーン(Engine::Scene::SceneAmbient)が持つ。
//
// ・チリだけは「送る」ではなく「出す」
//     チリはレンダーパスの設定ではなくエフェクトなので、ECS のエンティティを
//     1つ作って持ち続け、その位置を毎フレームカメラへ寄せる。
//     出したエンティティの始末もこのオブジェクトの仕事(Release で返す)。
//==========================================================================================
namespace App::Object
{
	//======================================================================================
	// 更新 : チリをカメラへ追従させる
	//
	// カメラが center から length より離れたら、speed で「length だけ離れたところ」まで詰める。
	// 詰め切ったら止まるので、チリの塊はカメラの後ろを引きずるように付いてくる。
	//
	// ・追従は距離で判定する。毎フレーム無条件にカメラへ寄せると、
	//   チリがカメラと一緒に動いてしまい「自分が進んでいる感じ」が出ない。
	// ・カメラが見つからないフレームは何もしない(その場に置いたままにする)。
	//   シーンの切り替え中など、一瞬カメラが居ないフレームがあるため
	//======================================================================================
	void AmbientDustObject::Update(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;

		// 未設定なら出しているものを片付けて終わり
		if (m_dast.effectGUID == Core::DEFAULT_GUID)
		{
			ReleaseDastEntity(a_context);
			return;
		}

		Math::Vector3 _camPos = {};
		if (!TryGetMainCameraPos(a_context, _camPos)) return;

		if (!m_isDastCentered)
		{
			// 初回はカメラの位置へ置く。
			// ここを追従に任せると、原点からカメラまでの距離を延々となめることになる
			m_dast.center = _camPos;
			m_isDastCentered = true;
		}
		else
		{
			const Math::Vector3 _toCam = _camPos - m_dast.center;
			const float _dist = _toCam.Length();
			const float _length = std::max(m_dast.length, 0.0f);

			if (_dist > _length && _dist > 1e-6f)
			{
				// 詰めてよいのは「length だけ離れたところ」まで。
				// speed が 0 以下なら追従の意味が無いので、その場で詰め切る
				const float _remain = _dist - _length;
				const float _step = (m_dast.speed > 0.0f)
					? std::min(m_dast.speed * a_context.dt, _remain)
					: _remain;

				m_dast.center = m_dast.center + (_toCam / _dist) * _step;
			}
		}

		EnsureDastEntity(a_context);
		ApplyDastToEntity(a_context);
	}

	//======================================================================================
	// 解放 : 出しているチリを片付ける
	//
	// 置いていくと、シーンを抜けたあとも空中にチリだけが残る
	//======================================================================================
	void AmbientDustObject::Release(Engine::GameObject::ObjectContext& a_context)
	{
		ReleaseDastEntity(a_context);
	}

	void AmbientDustObject::Archive(Engine::Persistence::Archive& a_ar, Engine::GameObject::ObjectContext& a_context)
	{
		// ---- 空気中のチリ ----
		// center は追従の結果なので保存しない(最初のフレームでカメラの位置へ置き直す)
		a_ar.GUIDField("DastEffectGUID", m_dast.effectGUID);
		a_ar.Field("DastColorScale", m_dast.m_colorScale);
		a_ar.Field("DastScale", m_dast.scale);
		a_ar.Field("DastFollowLength", m_dast.length);
		a_ar.Field("DastFollowSpeed", m_dast.speed);

		// 読み込み時はエフェクトの読み込みを始めさせる。
		// 実体が届くのを待つ必要はないので要求だけ出して先へ進む
		if (a_ar.IsLoading())
		{
			// 読み込み直後はまだカメラの位置が分からないので、
			// 追従の初期化からやり直させる(最初のフレームでカメラへ置かれる)
			m_isDastCentered = false;

			if (!a_context.pServices || !a_context.pServices->pResourceManager) return;

			// チリのエフェクト。実体を使うのはエンティティ側(EffectAssetComponent)なので、
			// こちらが握るのはインスペクターの表示用 ＋ 読み込みを始めさせるため
			if (m_dast.effectGUID != Core::DEFAULT_GUID)
			{
				m_dast.m_effectAsset =
					a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::EffectAsset>(m_dast.effectGUID);
			}
		}
	}

	//======================================================================================
	// メインカメラのワールド座標
	//
	// どれがメインカメラかは MainCameraSystem が決めて
	// SingletonEntityResource へ置いてあるので、ここでは探さずに引くだけ
	//======================================================================================
	bool AmbientDustObject::TryGetMainCameraPos(
		Engine::GameObject::ObjectContext& a_context, Math::Vector3& a_outPos) const
	{
		if (!a_context.pWorld) return false;
		if (!a_context.pWorld->HasResource<InstanceResource::SingletonEntityResource>()) return false;

		const Engine::ECS::Entity _camera =
			a_context.pWorld->RefResource<InstanceResource::SingletonEntityResource>().mainCamera;

		if (!a_context.pWorld->IsAliveEntity(_camera)) return false;
		if (!a_context.pWorld->HasComponent<Component::WorldMatrixComponent>(_camera)) return false;

		const auto* _pWorldMat = a_context.pWorld->RefData<Component::WorldMatrixComponent>(_camera);
		if (!_pWorldMat) return false;

		a_outPos = Math::Matrix(_pWorldMat->worldMat).Translation();
		return true;
	}

	//======================================================================================
	// チリのエンティティを用意する
	//
	// 出しっぱなしのエフェクトなので destroyOnFinish は立てない。
	// 消すのはこちらの都合(設定の差し替え・シーンの終わり)だけ。
	//
	// エフェクトを差し替えられたら作り直す。中身を書き換えても
	// アセットのハンドルを取り直すのは PostDeserialize なので、
	// 作り直したほうが素直に済む
	//======================================================================================
	void AmbientDustObject::EnsureDastEntity(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;

		// 生きていて、しかも今の設定で出したものならそのまま使う
		if (a_context.pWorld->IsAliveEntity(m_dastEntity) &&
			m_dastSpawnedGUID == m_dast.effectGUID)
		{
			return;
		}

		// 古いものを片付ける(もう居なければ何も起きない)
		ReleaseDastEntity(a_context);

		// 即時生成で作る。GameObject の Update は ECS の反復の外なので呼んでよい。
		// 遅延生成だとエンティティが返らず、追従させる相手を握れない
		m_dastEntity = App::Utility::SpawnEffectAtNow(
			*a_context.pWorld,
			m_dast.effectGUID,
			m_dast.center,
			false,						// 出し切っても消さない(寿命はこちらが握る)
			{},							// 向きはアセットのパーツ側に任せる
			std::max(m_dast.scale, 0.01f));

		m_dastSpawnedGUID = (m_dastEntity != Engine::ECS::Limits::INVALID_ENTITY)
			? m_dast.effectGUID
			: Core::DEFAULT_GUID;
	}

	//======================================================================================
	// 追従の結果をチリのエンティティへ書き込む
	//
	// ワールド行列も一緒に入れる。GameObject の Update は CalcMatrixSystem(PostUpdate)の
	// 後に走るので、ローカル座標だけ書いても絵に反映されるのは次のフレームになる。
	// チリはカメラと一緒に動く相手なので、その1フレームがそのまま置いていかれた見た目になる
	//======================================================================================
	void AmbientDustObject::ApplyDastToEntity(Engine::GameObject::ObjectContext& a_context)
	{
		if (!a_context.pWorld) return;
		if (!a_context.pWorld->IsAliveEntity(m_dastEntity)) return;

		if (a_context.pWorld->HasComponent<Component::LocalTransformComponent>(m_dastEntity))
		{
			if (auto* _pTrs = a_context.pWorld->RefData<Component::LocalTransformComponent>(m_dastEntity))
			{
				_pTrs->pos = m_dast.center;
				_pTrs->isDirty = true;
			}
		}

		if (a_context.pWorld->HasComponent<Component::WorldMatrixComponent>(m_dastEntity))
		{
			if (auto* _pWorldMat = a_context.pWorld->RefData<Component::WorldMatrixComponent>(m_dastEntity))
			{
				_pWorldMat->worldMat = Math::Matrix::CreateTranslation(m_dast.center);
			}
		}

		// 出現空間の広さ。エフェクト全体の倍率なので、
		// ばらつき半径と一緒に粒の大きさにも掛かる(EffectDrawSystem)
		if (a_context.pWorld->HasComponent<Component::EffectOverrideComponent>(m_dastEntity))
		{
			if (auto* _pEffect = a_context.pWorld->RefData<Component::EffectOverrideComponent>(m_dastEntity))
			{
				_pEffect->effectScale = std::max(m_dast.scale, 0.01f);
			}
		}
	}

	//======================================================================================
	// チリのエンティティを手放す
	//
	// 解放予約だけ。実際に消えるのは次の BeginFrame で、その前に Release フェーズが
	// 走るのでパーティクルの発生源の席も返ってから消える
	//======================================================================================
	void AmbientDustObject::ReleaseDastEntity(Engine::GameObject::ObjectContext& a_context)
	{
		if (a_context.pWorld && a_context.pWorld->IsAliveEntity(m_dastEntity))
		{
			a_context.pWorld->ReserveReleaseEntity(m_dastEntity);
		}

		m_dastEntity = Engine::ECS::Limits::INVALID_ENTITY;
		m_dastSpawnedGUID = Core::DEFAULT_GUID;
	}

	//======================================================================================
	// インスペクター
	//======================================================================================
	void AmbientDustObject::DrawInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::HelpText("カメラに追従する空間のチリ。環境光・フォグ・空は SceneSettingsPanel で設定する");
		Engine::EditorField::Line();

		DrawDastInspector(a_context);
	}

	//======================================================================================
	// 空気中のチリ
	//======================================================================================
	void AmbientDustObject::DrawDastInspector(Engine::GameObject::ObjectContext& a_context)
	{
		Engine::EditorField::Header("Dast");

		if (!a_context.pServices || !a_context.pServices->pResourceManager)
		{
			Engine::EditorField::WarningText("ResourceManager is null");
			return;
		}

		// 差し替えたら次の Update が古いエンティティを片付けて出し直す
		if (Engine::EditorField::AssetField(
			*a_context.pServices,
			"Dast Effect", "EffectAsset", m_dast.effectGUID))
		{
			m_dast.m_effectAsset = (m_dast.effectGUID != Core::DEFAULT_GUID)
				? a_context.pServices->pResourceManager->RequestLoad<Engine::Resource::EffectAsset>(m_dast.effectGUID)
				: Engine::ResourceRef<Engine::Resource::EffectAsset>{};

			// 置き直す : 別のエフェクトはカメラの位置から出し始めたい
			m_isDastCentered = false;
		}

		if (m_dast.effectGUID == Core::DEFAULT_GUID)
		{
			Engine::EditorField::HelpText("(未設定 : チリは出ません)");
			return;
		}

		// 中身の確認だけ。絵の編集はエフェクトアセット側のインスペクターで行う。
		// ここで実体を握っているのは、シーンに入った瞬間からチリが出るように
		// 読み込みを先に始めさせるため(出すのはエンティティ側のハンドル)
		if (const auto* _pEffect = a_context.pServices->pResourceManager->Ref(m_dast.m_effectAsset))
		{
			Engine::EditorField::Value("Particle Parts", "%d", static_cast<int>(_pEffect->GetParticleParts().size()));
			Engine::EditorField::Value("Mesh Parts", "%d", static_cast<int>(_pEffect->GetMeshParts().size()));
		}
		else
		{
			Engine::EditorField::HelpText("(読み込み中)");
		}

		// 出現空間の広さ。エフェクト全体の倍率として渡すので、
		// ばらつき半径だけでなく粒の大きさにも掛かる
		Engine::EditorField::Field("Volume Scale", m_dast.scale, 0.1f, 0.01f, 10000.0f);
		if (m_dast.scale < 0.01f) m_dast.scale = 0.01f;

		// 色スケール : まだ絵には効かない。
		// 粒の色はパーティクルアセットの定数バッファ(全員で共有)が持っているので、
		// 個体ごとに掛けるには描画側に受け口を足す必要がある
		Engine::EditorField::ColorField("Color Scale", m_dast.m_colorScale);
		Engine::EditorField::Tooltip("(色はパーティクルアセット側。ここはまだ絵に反映されません)");

		// カメラがこの距離だけ離れたら追従を始める。
		// 0 にすると常にカメラへ張り付くので、進んでいる感じが出なくなる
		Engine::EditorField::Field("Follow Length", m_dast.length, 0.1f, 0.0f, 10000.0f);
		if (m_dast.length < 0.0f) m_dast.length = 0.0f;

		// 追従スピード(毎秒)。0 以下ならその場で詰め切る(＝常に張り付く)
		Engine::EditorField::Field("Follow Speed", m_dast.speed, 0.1f, 0.0f, 10000.0f);
		if (m_dast.speed < 0.0f) m_dast.speed = 0.0f;

		// 今どこに居るか。追従の具合を見るための表示なので触らせない
		Engine::EditorField::Value("Center", "%.1f, %.1f, %.1f", m_dast.center.x, m_dast.center.y, m_dast.center.z);

		if (!a_context.pWorld || !a_context.pWorld->IsAliveEntity(m_dastEntity))
		{
			Engine::EditorField::HelpText("(まだ出ていません : 次の更新で出ます)");
		}

		if (Engine::EditorField::Button("Reset Center"))
		{
			// 次の更新でカメラの位置へ置き直す
			m_isDastCentered = false;
		}
	}
}
