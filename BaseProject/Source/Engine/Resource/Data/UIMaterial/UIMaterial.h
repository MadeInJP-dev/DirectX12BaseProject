#pragma once

#include "Engine/Graphics/Frame/DrawList/UIData.h"	// EUIEffect

namespace Engine::Resource
{
	class ResourceManager;

	//==========================================================================================
	// UIマテリアル : UIの見た目に掛ける効果の設計図
	//
	// UI(UIBase)が1つ指して、そのUIの飾り全部(枠・画像・文字)へ同じ効果を掛ける。
	// 3D 用の Material(PBR の値を持つ)とは別物で、UIPS のどの効果を使うかと、その設定だけを持つ。
	//
	// ・持つのは「どう効かせるか」だけで、「いまどれだけ効いているか」(ディゾルブの進み具合)は持たない。
	//   進み具合は使う側(UIの演出 UITransition など)がその場で渡す。
	//   アセットはロード結果を全員で共有するので、進み具合をここに持たせると
	//   同じマテリアルを指すUIが全部いっしょに消えてしまうため
	// ・効果は今のところ UIPS に組み込みのもの(EUIEffect)から選ぶ。
	//   任意のシェーダーを書けるようにするときは、ここにシェーダーを持たせて
	//   UIPass が描くときに PSO を切り替える形へ広げる
	//==========================================================================================
	struct UIMaterial
	{
		UIMaterial() = default;
		explicit UIMaterial(const std::string& a_name) : name(a_name) {}
		~UIMaterial() = default;
		NON_COPYABLE_MOVABLE(UIMaterial);

		//---------------------------------------
		// 設定(保存される)
		//---------------------------------------
		std::string name = "";

		// 掛ける効果
		Graphics::EUIEffect effect = Graphics::EUIEffect::Dissolve;

		// ノイズ(ディゾルブの消え方の模様)。空ならシェーダー内で作ったノイズを使う
		Core::GUID noiseTexGUID = {};
		float noisePixel = 128.0f;					// ノイズ1枚ぶんの大きさ(px)。小さいほど細かく崩れる

		// 縁(消えかけのところに乗せる色)
		float edgeWidth = 0.08f;					// 縁の幅(ノイズの値の幅, 0〜1)
		Math::Color edgeColor = { 1.0f, 0.55f, 0.15f, 1.0f };	// 縁の色(a は乗せる強さ)

		// スキャンライン(ScanLinePass)を掛けるか。UIAnchor の IsScanLine と同じ印を立てる
		bool isScanLine = false;

		//---------------------------------------
		// ランタイム(保存しない)
		//---------------------------------------
		ResourceRef<Texture> noiseTexRef = {};

		//---------------------------------------
		// 操作
		//---------------------------------------

		// アーカイブ。フィールドを足すときは必ず末尾へ(.ob* は並び順で読むため)
		void Archive(Persistence::Archive& a_ar);

		// 拡張子なしのベースパスへ保存する
		void Save(const std::string& a_baseFilePath);

		// 保存されているGUIDから、ノイズのテクスチャを引き直す(実体の到着は待たない)
		void ResolveReferences(ResourceManager& a_resourceManager);
	};
}
