#pragma once
namespace Engine::Graphics
{
	
	enum class EShaderPermutationFlags : uint32_t
	{
		None				= 0,
		Static				= 1 << 0,	// スタティックモデルか
		Skinned				= 1 << 1,	// アニメーションモデルか
		AlphaMasked			= 1 << 2,	// アルファテストありか
		UseNormalMap		= 1 << 3,	// 法線マップを使っているか
		DepthMasked			= 1 << 4,	// 深度テストありか
		UseGPUInstancing	= 1 << 5,	// GPUインスタンシング使用するか
		MeshShader			= 1 << 6,	// メッシュシェーダー
		DitherFade			= 1 << 7,	// フェード中(インスタンスの fade が 1 未満)。深度だけのパスでもディザーで画素を捨てる

	};

	struct PSOKey
	{
		uint32_t permutationFlags;						// マテリアルやモデル、エンティティの状態
		Handle<Resource::Shader> psHandle = {};			// このパスが使うピクセルシェーダー
		// ハッシュ計算用
		bool operator==(const PSOKey& other) const {
			return	permutationFlags == other.permutationFlags &&
					psHandle == other.psHandle;
		}
	};
}

namespace std
{
	template <>
	struct hash<Engine::Graphics::PSOKey>
	{
		size_t operator()(const Engine::Graphics::PSOKey& key) const
		{
			uint64_t _hash = 14695981039346656037ull;
			auto UpdateHash = [&](const void* p, size_t size) {
				const uint8_t* ptr = static_cast<const uint8_t*>(p);
				for (size_t i = 0; i < size; ++i) {
					_hash ^= ptr[i];
					_hash *= 1099511628211ull;
				}
				};

			UpdateHash(&key.permutationFlags, sizeof(key.permutationFlags));
			UpdateHash(&key.psHandle.id, sizeof(key.psHandle.id));

			return static_cast<size_t>(_hash);
		}
	};
}