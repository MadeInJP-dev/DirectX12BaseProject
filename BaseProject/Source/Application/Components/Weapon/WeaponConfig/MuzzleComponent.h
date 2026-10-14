#pragma once
namespace App::Component
{
	//================================================================================
	// 武器には必須 マズル位置を示すコンポーネント
	// 
	// 武器側にヌルポインタノードを仕込む前提の設計
	// 弾丸の発射位置やエフェクトを炊く位置もマズル箇所となる
	//================================================================================
	struct MuzzleComponent
	{
		Core::Math::Vector3 dir = {};		// マズル方向 : 発射方向となる

		UINT muzzlePtrNodeHash = 0;			// 武器モデルのマズル位置ヌルポインタノードハッシュ
		UINT nodeIndex = 0;					// ランタイム中のノードインデックス
		Core::Math::Vector3 offset = {};	// オフセット値

		struct Effect
		{
			Core::GUID guid = Core::DEFAULT_GUID;
			Engine::Handle<Engine::Resource::EffectAsset> handle = {};
		};

		Effect muzzleEffect = {};			// マズルフラッシュエフェクト
	};

}