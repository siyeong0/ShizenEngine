// ============================================================================
// MaterialEditorAssetFacade.h
// ============================================================================

#pragma once

#include <string>
#include <memory>

#include "Engine/AssetRuntime/AssetManager/Public/AssetManager.h"
#include "Engine/AssetRuntime/Common/AssetRef.hpp"
#include "Engine/AssetRuntime/Common/AssetPtr.hpp"

#include "Engine/AssetRuntime/Common/AssetTypeTraits.h"

#include "Engine/AssetRuntime/AssetData/Public/StaticMeshAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/TextureAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/MaterialAsset.h"
#include "Engine/AssetRuntime/AssetData/Public/AssimpAsset.h"

namespace shz
{
	enum class EEditorAssetKind : uint8
	{
		Unknown = 0,
		AssimpMesh,
		StaticMesh,
		Material,
		Texture,
	};

	struct LoadedCpuMesh final
	{
		// 최종적으로 MaterialEditor가 프리뷰로 사용할 CPU StaticMesh
		// - StaticMesh 파일이면: StaticMeshPtr가 유효
		// - Assimp 파일이면: AssimpPtr + BuiltMesh가 유효(StaticMeshAsset로 빌드)
		AssetPtr<StaticMeshAsset> StaticMeshPtr = {};
		AssetPtr<AssimpAsset>     AssimpPtr = {};

		// Assimp → Build 결과물 보관(AssetPtr이 아니라 editor 소유)
		std::unique_ptr<StaticMeshAsset> BuiltMesh = nullptr;

		const StaticMeshAsset* GetMesh() const noexcept
		{
			if (StaticMeshPtr) return StaticMeshPtr.Get();
			if (BuiltMesh)     return BuiltMesh.get();
			return nullptr;
		}

		bool IsValid() const noexcept { return GetMesh() != nullptr; }
	};

	class MaterialEditorAssetFacade final
	{
	public:
		explicit MaterialEditorAssetFacade(AssetManager* pAM) : m_pAM(pAM) {}

		void RegisterDefaultImporters();

		static std::string SanitizeFilePath(std::string s);
		static std::string GetLowerExt(const std::string& path);
		static EEditorAssetKind ClassifyPathByExtension(const std::string& path);

		// Mesh 로드(확장자 기반)
		bool LoadCpuMeshBlocking(
			const std::string& path,
			LoadedCpuMesh& outMesh,
			std::string* outError = nullptr,
			EAssetLoadFlags flags = EAssetLoadFlags::None);

		// Material 로드
		bool LoadMaterialBlocking(
			const std::string& path,
			AssetPtr<MaterialAsset>& outMat,
			std::string* outError = nullptr,
			EAssetLoadFlags flags = EAssetLoadFlags::None);

		// Texture 로드(필요 시)
		bool LoadTextureBlocking(
			const std::string& path,
			AssetPtr<TextureAsset>& outTex,
			std::string* outError = nullptr,
			EAssetLoadFlags flags = EAssetLoadFlags::None);

		AssetManager* GetAssetManager() const noexcept { return m_pAM; }

	private:
		AssetManager* m_pAM = nullptr;
	};
} // namespace shz
