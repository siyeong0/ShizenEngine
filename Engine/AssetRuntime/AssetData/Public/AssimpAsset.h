#pragma once
#include <string>
#include <memory>

namespace Assimp { class Importer; }
struct aiScene;

namespace shz
{
	class AssimpAsset final
	{
	public:
		std::string SourcePath = {};

		// Keep importer alive => aiScene pointer remains valid.
		std::shared_ptr<Assimp::Importer> Importer = {};
		const aiScene* Scene = nullptr;

	public:
		void Clear()
		{
			SourcePath.clear();
			Scene = nullptr;
			Importer.reset();
		}

		bool IsValid() const noexcept
		{
			return (Scene != nullptr) && (Importer != nullptr);
		}
	};
} // namespace shz
