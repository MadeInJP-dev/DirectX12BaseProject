#pragma once

#include "Core/Math/Vector/Vector3.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Ray.h"
#include "Core/BodyID.h"

namespace JPH
{
	class PhysicsSystem;
	class BroadPhaseLayerInterface;
	class ObjectVsBroadPhaseLayerFilter;
	class ObjectLayerPairFilter;
}

namespace Engine::Resource
{
	class ResourceManager;
	class Model;
}

namespace Engine::Graphics
{
	class DebugDraw;
}

namespace Engine::Physics
{
	class PhysicsEngine;

	// PhysicsWorld を作るときの大きさ。
	// PhysicsSystem::Init で先に確保されるので、プレビュー用のワールドは小さくしておく
	struct PhysicsWorldDesc
	{
		uint32_t maxBodies				= 65536;	// ボイド4000体 + 地形 + 弾 を余裕で収める
		uint32_t maxBodyPairs			= 65536;
		uint32_t maxContactConstraints	= 10240;
		Math::Vector3 gravity			= { 0.0f, -9.81f, 0.0f };	// 作った直後の重力。シーンを開くと ScenePhysics の値で上書きされる

		// エフェクトエディターのプレビュー用
		static PhysicsWorldDesc MakePreview() noexcept
		{
			PhysicsWorldDesc _desc;
			_desc.maxBodies = 1024;
			_desc.maxBodyPairs = 1024;
			_desc.maxContactConstraints = 1024;
			return _desc;
		}
	};

	// クエリで全レイヤーを相手にするときのマスク。
	// アプリの Layer のビットはこの範囲(7ビット)に収めること
	inline constexpr uint32_t QUERY_ALL_LAYERS = 0x7Fu;

	// レイが当たったところ
	struct RayHit
	{
		ECS::Entity entity = ECS::Limits::INVALID_ENTITY;	// 当たったボディの持ち主
		Math::Vector3 position = {};	// 当たった位置(ワールド)
		Math::Vector3 normal = {};		// 当たった面の法線(ワールド・単位長)
		float distance = 0.0f;			// 始点からの距離
	};

	// 形状(球など)が当たったところ
	struct ShapeHit
	{
		ECS::Entity entity = ECS::Limits::INVALID_ENTITY;	// 当たったボディの持ち主
		Math::Vector3 position = {};	// 相手の表面の接触点(ワールド)
		Math::Vector3 normal = {};		// 相手の表面の法線(こちらを向く・単位長)
		float fraction = 0.0f;			// 掃いたときの、当たった位置までの割合(0 = 始点で既に重なっていた)
	};

	// モデルからボディを作るときの形状
	enum class EModelBodyShape : uint8_t
	{
		CollisionMesh,	// 判定メッシュ(COL ノード)の三角形
		DrawBounds,		// 描画メッシュ全体のAABBの箱(寸法を持たない Mesh 以外の形状=弾・ミサイル・ボイド)
	};

	// モデルからボディを作るときに渡すもの
	struct ModelBodyDesc
	{
		ECS::Entity owner = ECS::Limits::INVALID_ENTITY;	// 持ち主。削除のときに照合する
		Handle<Resource::Model> modelHandle = {};			// 形状の元。同じモデルの形状はワールドの中で使い回す
		Math::Matrix worldMat = {};							// 親込みのワールド行列

		EModelBodyShape shape = EModelBodyShape::CollisionMesh;

		// 動くボディか。動くものは Kinematic で作り、SetBodyTransform で毎フレーム位置を合わせる
		bool isMoving = false;

		uint32_t group = 0;		// 自分のレイヤー(アプリの ColliderComponent::layer のビット)
		uint32_t mask = 0;		// 当たりに行く相手(collideLayer のビット)
	};

	//======================================================================================
	// シーン(ECSワールド)ごとの物理空間
	//
	// World のリソースとして持つ(CreateSceneWorld で足す)。
	// システムからは a_ctx.pWorld->RefResource<Engine::Physics::PhysicsWorld>() で引く。
	//
	// Jolt の型は外へ出さない。ボディの出し入れやクエリはここに口を足していく
	//======================================================================================
	class PhysicsWorld
	{
	public:

		// a_pEngine : Jolt 全体の持ち主(借り物)。このワールドより長生きすること
		PhysicsWorld(PhysicsEngine* a_pEngine, const PhysicsWorldDesc& a_desc);
		~PhysicsWorld();
		NON_COPYABLE_NON_MOVABLE(PhysicsWorld);

		// 1ステップ進める。判定クエリ(Physics フェーズ)の前に呼ぶ。
		// 作ったボディの空間への追加もここでまとめて行う
		void Update(float a_dt);

		// 追加待ちのボディを空間へ入れる(進めはしない)。
		// Update の中でも呼ばれる。シーンの読み込み中は Update を回さないので、
		// 作ったボディを読み込みのうちに入れておくためにこれだけを呼ぶ
		void FlushPendingBodies();

		//----------------------------------------------------------------------------------
		// ボディ
		//----------------------------------------------------------------------------------

		// モデルからボディを作る。
		// 空間へ入るのは次の Update(Start で作れば、そのフレームの判定クエリに間に合う)。
		// 作れなかったときは無効な札を返す
		BodyHandle CreateModelBody(const Resource::ResourceManager& a_resourceManager, const ModelBodyDesc& a_desc);

		// 動くボディの位置・向き・拡大率を合わせる(瞬間移動)。
		// Update フェーズ(SyncPhysicsBodySystem)で毎フレーム呼ぶ。
		// a_owner が作ったときの持ち主と違えば何もしない。
		// ボディごとにロックを取るので、別々のボディなら複数のスレッドから同時に呼んでよい
		// (クエリや PhysicsSystem::Update とは同時に呼ばないこと。依存の宣言で離してある)
		void SetBodyTransform(BodyHandle a_handle, ECS::Entity a_owner, const Math::Matrix& a_worldMat);

		// ボディを消す。a_owner が作ったときの持ち主と違えば何もしない
		// (コンポーネントの中身ごと複製されたエンティティが、他人のボディを消さないため)
		void DestroyBody(BodyHandle a_handle, ECS::Entity a_owner);

		//----------------------------------------------------------------------------------
		// クエリ
		//
		// a_queryMask : 当たりに行く相手のレイヤー(ビット和)。ボディ側の mask は見ない。
		//               0 なら何も調べずに「当たらない」を返す
		// a_ignore    : 判定から外す持ち主(自分自身)
		// 三角形は表裏どちらにも当たる
		//----------------------------------------------------------------------------------

		// レイ。いちばん手前の1つを返す。方向は正規化しなくてよい
		bool CastRay(const Math::Ray& a_ray, uint32_t a_queryMask, ECS::Entity a_ignore, RayHit& a_outHit) const;

		// カプセル(線分 A-B + 半径)を押し出す。反復して床と壁などを順に解決する。
		// a_pointA / a_pointB は押し出し後の位置に更新され、a_outCorrection に合計の補正が入る。
		// 押し出す相手は判定メッシュのボディだけ(箱で概算しているもの = 弾・ボイドからは押し出さない)
		bool ResolveCapsule(Math::Vector3& a_pointA, Math::Vector3& a_pointB, float a_radius,
			uint32_t a_queryMask, ECS::Entity a_ignore, Math::Vector3& a_outCorrection, int a_iterations = 4) const;

		// 球を押し出す。a_center は押し出し後の位置に更新される
		bool ResolveSphere(Math::Vector3& a_center, float a_radius,
			uint32_t a_queryMask, ECS::Entity a_ignore, Math::Vector3& a_outCorrection, int a_iterations = 4) const;

		// 球を a_from から a_to へ掃き、進む向きでいちばん手前に当たった相手を返す(弾の当たり判定)。
		// 始点で既に重なっていれば、それを返す(fraction = 0)。
		// a_ignore2 : もう1体外す持ち主(弾から見た発射元。銃口は撃った本人の体の中にある)
		bool SweepSphere(const Math::Vector3& a_from, const Math::Vector3& a_to, float a_radius,
			uint32_t a_queryMask, ECS::Entity a_ignore, ECS::Entity a_ignore2, ShapeHit& a_outHit) const;

		// 球の重なり。いちばん深く重なっている相手を返す
		bool OverlapSphere(const Math::Vector3& a_center, float a_radius,
			uint32_t a_queryMask, ECS::Entity a_ignore, ECS::Entity a_ignore2, ShapeHit& a_outHit) const;

		// 球の重なり。重なっている相手を全部 a_outHits へ足す(爆風の範囲ダメージ)。
		// 1つの持ち主が複数のボディを持っていても1件にまとめる(いちばん深く重なったもの)。
		// a_outHits は空にしないで後ろへ足す。1件でも足したら true
		bool OverlapSphereAll(const Math::Vector3& a_center, float a_radius,
			uint32_t a_queryMask, ECS::Entity a_ignore, ECS::Entity a_ignore2, std::vector<ShapeHit>& a_outHits) const;

		//----------------------------------------------------------------------------------
		// 重力
		//
		// 値の持ち主はシーン(Engine::Scene::ScenePhysics)。読み込んだときと編集したときに流し込まれる。
		// 自前で落下を解くシステム(DeathMotion / Ballistic など)もここから引く。
		// 書くのはメインスレッドのシステムの外だけなので、システムからは読むだけでよい
		//----------------------------------------------------------------------------------
		void SetGravity(const Math::Vector3& a_gravity);
		const Math::Vector3& GetGravity() const { return m_gravity; }

		//----------------------------------------------------------------------------------
		// 確認用
		//----------------------------------------------------------------------------------

		// 登録されているボディの数
		uint32_t GetBodyCount() const;

		// 判定メッシュのボディのワールドAABBを積む(静的=水色、動く=黄色)。
		// 箱で概算しているもの(弾・ボイド)は数が多く、線の上限を食い潰すので描かない。
		// 表示の可否は DebugDraw 側のオプションが決める
		void DrawDebug(Graphics::DebugDraw* a_pDebugDraw) const;

		bool IsValid() const { return m_upPhysicsSystem != nullptr; }

	private:

	private:

		PhysicsEngine* m_pEngine = nullptr;

		// 重力(Jolt へも同じ値を渡している)
		Math::Vector3 m_gravity = { 0.0f, -9.81f, 0.0f };

		// PhysicsSystem はレイヤー定義を参照で持つので、こちらを先に宣言する
		// (メンバーは宣言の逆順に壊れる = PhysicsSystem が先に消える)
		std::unique_ptr<JPH::BroadPhaseLayerInterface> m_upBroadPhaseLayerInterface;
		std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> m_upObjectVsBroadPhaseLayerFilter;
		std::unique_ptr<JPH::ObjectLayerPairFilter> m_upObjectLayerPairFilter;

		std::unique_ptr<JPH::PhysicsSystem> m_upPhysicsSystem;

		// 形状の使い回しと追加待ちの一覧(Jolt の型を持つので中身は .cpp)
		struct Detail;
		std::unique_ptr<Detail> m_upDetail;
	};
}
