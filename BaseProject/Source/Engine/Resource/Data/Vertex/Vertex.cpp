#include "Engine/Resource/Data/Vertex/Vertex.h"

namespace Engine::Resource
{
	namespace
	{
		// [-1, 1] を snorm16 にする
		uint32_t ToSnorm16(float a_value)
		{
			const float _clamped = (std::clamp)(a_value, -1.0f, 1.0f);
			const int32_t _scaled = static_cast<int32_t>(std::lround(_clamped * 32767.0f));
			return static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(_scaled)));
		}

		// [0, 1] を unorm16 にする
		uint16_t ToUnorm16(float a_value)
		{
			const float _clamped = (std::clamp)(a_value, 0.0f, 1.0f);
			return static_cast<uint16_t>(std::lround(_clamped * 65535.0f));
		}

		// [0, 1] を unorm8 にする
		uint32_t ToUnorm8(float a_value)
		{
			const float _clamped = (std::clamp)(a_value, 0.0f, 1.0f);
			return static_cast<uint32_t>(std::lround(_clamped * 255.0f));
		}

		//--------------------------------------------------------------------------------------
		// 単位ベクトルを八面体符号化して 32 ビットに詰める(下位 16 ビットが x、上位が y)
		//
		// 球面を八面体へ投影し、下半分を折り返して正方形へ開く。
		// 復号はシェーダーの DecodeOctNormal(Vertex.hlsli)が行う
		//--------------------------------------------------------------------------------------
		uint32_t EncodeOctNormal(const Math::Vector3& a_vector)
		{
			const float _l1 = std::abs(a_vector.x) + std::abs(a_vector.y) + std::abs(a_vector.z);

			// 長さが無いものは向きを決められないので +Z にしておく
			if (_l1 <= 1e-20f) return ToSnorm16(0.0f) | (ToSnorm16(0.0f) << 16);

			float _x = a_vector.x / _l1;
			float _y = a_vector.y / _l1;

			// 下半分(z < 0)は外側へ折り返す
			if (a_vector.z < 0.0f)
			{
				const float _foldX = (1.0f - std::abs(_y)) * (_x >= 0.0f ? 1.0f : -1.0f);
				const float _foldY = (1.0f - std::abs(_x)) * (_y >= 0.0f ? 1.0f : -1.0f);
				_x = _foldX;
				_y = _foldY;
			}
			return ToSnorm16(_x) | (ToSnorm16(_y) << 16);
		}
	}

	MeshVertexPacked MeshVertexPacked::From(const MeshVertexFloat& a_src)
	{
		MeshVertexPacked _dst = {};
		_dst.pos = a_src.pos;
		_dst.normal = EncodeOctNormal(a_src.normal);
		_dst.tangent = EncodeOctNormal(a_src.tangent);
		_dst.uv = a_src.uv;
		_dst.color =
			ToUnorm8(a_src.color.r) |
			(ToUnorm8(a_src.color.g) << 8) |
			(ToUnorm8(a_src.color.b) << 16) |
			(ToUnorm8(a_src.color.a) << 24);
		return _dst;
	}

	MeshSkinVertex MeshSkinVertex::From(const MeshVertexFloat& a_src)
	{
		MeshSkinVertex _dst = {};
		for (size_t _i = 0; _i < _dst.boneIndex.size(); ++_i)
		{
			// 読み込み側は short で持っているが、シェーダーは 16 ビットの符号なしとして読む(以前と同じ)
			_dst.boneIndex[_i] = static_cast<uint16_t>(a_src.skinIndexList[_i]);
			_dst.boneWeight[_i] = ToUnorm16(a_src.skinWeightList[_i]);
		}
		return _dst;
	}
}
