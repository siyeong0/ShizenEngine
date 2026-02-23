#include "GrassViewer.h"

#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <unordered_set>

#include "ThirdParty/imgui/imgui.h"
#include "Engine/ImGui/Public/imGuIZMO.h"

#include "Engine/RuntimeData/Public/StaticMeshImporter.h"
#include "Engine/RuntimeData/Public/TextureImporter.h"
#include "Engine/RuntimeData/Public/MaterialImporter.h"
#include "Engine/RuntimeData/Public/MaterialManager.h"
#include "Engine/AssetManager/Public/AssimpImporter.h"

#include "Engine/Image/Public/TextureLoader.h"

namespace shz
{
	namespace hlsl
	{
#include "Shaders/HLSL_Structures.hlsli"
	} // namespace hlsl

	namespace
	{
		static void updatePrimaryView(
			ViewFamily& vf,
			const GrassViewer::ViewportState& vp,
			const FirstPersonCamera& cam)
		{
			ASSERT(!vf.Views.empty(), "No view.");

			auto& v = vf.Views[0];

			v.PrevViewMatrix = v.ViewMatrix;
			v.PrevProjMatrix = v.ProjMatrix;
			v.PrevViewProjMatrix = v.ViewProjMatrix;

			v.CameraPosition = cam.GetPos();

			v.ViewMatrix = cam.GetViewMatrix();
			v.ProjMatrix = cam.GetProjMatrix();
			v.ViewProjMatrix = v.ViewMatrix * v.ProjMatrix;

			v.FieldOfViewY = cam.GetProjAttribs().FOV;
			v.AspectRatio = cam.GetProjAttribs().AspectRatio;

			v.Viewport.left = 0;
			v.Viewport.top = 0;
			v.Viewport.right = vp.Width;
			v.Viewport.bottom = vp.Height;

			v.NearPlane = cam.GetProjAttribs().NearClipPlane;
			v.FarPlane = cam.GetProjAttribs().FarClipPlane;

			v.bOrthographic = false;
			v.OrthographicSize = 0.0f;
		}
	} // namespace

	SampleBase* CreateSample()
	{
		return new GrassViewer();
	}

	// ------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------

	void GrassViewer::Initialize(const SampleInitInfo& initInfo)
	{
		SampleBase::Initialize(initInfo);

		// Asset
		m_pAssetManager = std::make_unique<AssetManager>();
		{
			ASSERT(m_pAssetManager, "AssetManager is null.");
			m_pAssetManager->Initialize();
			m_pAssetManager->RegisterImporter(AssetTypeTraits<StaticMeshLevel>::TypeID, StaticMeshImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<Texture>::TypeID, TextureImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<Material>::TypeID, MaterialImporter{});
			m_pAssetManager->RegisterImporter(AssetTypeTraits<AssimpAsset>::TypeID, AssimpImporter{});
		}

		// Renderer + shader factory
		m_pRenderer = std::make_unique<Renderer>();
		{
			ASSERT(m_pRenderer, "Renderer is null.");

			ASSERT(m_pEngineFactory, "EngineFactory is null.");
			m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("C:/Dev/ShizenEngine/Shaders", &m_pShaderSourceFactory);
			ASSERT(m_pShaderSourceFactory, "ShaderSourceFactory is null.");

			ASSERT(m_pSwapChain, "SwapChain is null.");

			const auto scDesc = m_pSwapChain->GetDesc();
			m_Viewport.Width = std::max(1u, scDesc.Width);
			m_Viewport.Height = std::max(1u, scDesc.Height);

			RendererCreateInfo rendererCI = {};
			rendererCI.pEngineFactory = m_pEngineFactory;
			rendererCI.pShaderSourceFactory = m_pShaderSourceFactory;
			rendererCI.pDevice = m_pDevice;
			rendererCI.pImmediateContext = m_pImmediateContext;
			rendererCI.pDeferredContexts = m_pDeferredContexts;
			rendererCI.pSwapChain = m_pSwapChain;
			rendererCI.pImGui = m_pImGui;
			rendererCI.BackBufferWidth = m_Viewport.Width;
			rendererCI.BackBufferHeight = m_Viewport.Height;
			rendererCI.pAssetManager = m_pAssetManager.get();

			m_pRenderer->Initialize(rendererCI);
		}

		// -----------------------------------------------------------------
		// Terrain system
		// -----------------------------------------------------------------
		{
			m_pTerrainSystem = std::make_unique<TerrainSystem>();

			TerrainSystem::CreateInfo tci = {};

			tci.HeightPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/height.png";
			tci.DiffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/diffuse.png";
			tci.NormalPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/normal.png";
			tci.SlopePath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/slope.png";
			tci.FlowPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/flow.png";
			tci.RockyPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/rocky.png";
			tci.SoilPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/soil.png";
			tci.VegetationPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/vegetation.png";

			/*	tci.HeightPath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/height.png";
				tci.DiffusePath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/diffuse.png";
				tci.NormalPath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/normal.png";
				tci.SlopePath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/slope.png";
				tci.FlowPath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/flow.png";
				tci.RockyPath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/rocky.png";
				tci.SoilPath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/soil.png";
				tci.VegetationPath = "C:/Dev/ShizenEngine/Assets/Terrain/Eclipse/vegetation.png";*/

			tci.ChunkSize = 64.0f;
			tci.CellSize = 1.0f;
			tci.WorldSpacing = 1.22f;
			tci.HeightScale = 1500.0f;
			tci.HeightOffset = 0.0f;
			tci.bCenterXZ = true;

			//tci.ChunkSize = 64.0f;
			//tci.CellSize = 1.0f;
			//tci.WorldSpacing = 1.22f;
			//tci.HeightScale = 4000.0f;
			//tci.HeightOffset = 0.0f;
			//tci.bCenterXZ = true;

			tci.SoilMaterialPath = "C:/Dev/ShizenEngine/Assets/Materials/Gravel032_2K-PNG";
			tci.RockyMaterialPath = "C:/Dev/ShizenEngine/Assets/Materials/Rock030_4K-PNG";
			tci.GravelMaterialPath = "C:/Dev/ShizenEngine/Assets/Materials/Gravel015_2K-PNG";

			m_pTerrainSystem->Initialize(*m_pRenderer, *m_pAssetManager, tci);
		}

		// Render Scene
		{
			m_pRenderScene = std::make_unique<RenderScene>();
			ASSERT(m_pRenderScene, "RenderScene is null.");
		}

		// -----------------------------------------------------------------
		// Create render systems
		// -----------------------------------------------------------------
		{
			m_pInteractionSystem = std::make_unique<InteractionSystem>();
			m_pGrassSystem = std::make_unique<GrassSystem>();

			m_pInteractionSystem->Initialize(*m_pRenderer);
			m_pGrassSystem->Initialize(*m_pRenderer);

			// ------------------------------------------------------------
			// Grass model
			// ------------------------------------------------------------
			{
				m_pRenderer->RegisterMaterialTemplate("GrassMesh", "GrassMesh.vsh", "GrassMesh.psh", MATERIAL_BLEND_MODE_MASKED);
				m_pRenderer->RegisterMaterialTemplate("GrassCrossPlane", "GrassCrossPlane.vsh", "Grass.psh", MATERIAL_BLEND_MODE_MASKED);
				m_pRenderer->RegisterMaterialTemplate("GrassBillboard", "GrassBillboard.vsh", "Grass.psh", MATERIAL_BLEND_MODE_MASKED);

				auto uniform01 = [](StaticMeshLevel& mesh)
				{
					mesh.RecomputeBounds();
					const Box& b = mesh.GetBoxBounds();
					float yScale01 = 1.0f / (b.Max().y - b.Min().y);
					mesh.ApplyUniformScale(yScale01);
					mesh.MoveBottomToOrigin(true);
				};

				auto addGrass = [&](const std::string& path)
				{
					GrassDesc gd = {};
					StaticMesh grassMesh;
					// LOD0 : Mesh
					{
						AssetRef<AssimpAsset> grassMeshRef = m_pAssetManager->RegisterAsset<AssimpAsset>(path);
						const AssimpAsset& grassAssimp = *m_pAssetManager->LoadBlocking(grassMeshRef);

						StaticMeshLevel grassMeshLevel;
						BuildStaticMeshAsset(grassAssimp, &grassMeshLevel, {}, "GrassMesh", nullptr, m_pAssetManager.get());
						uniform01(grassMeshLevel);

						for (auto matId : grassMeshLevel.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD0"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);

							const Texture& baseColorTex = *m_pAssetManager->LoadBlocking(mat.GetTextureAssetRef("g_BaseColorTex"));
							ASSERT(baseColorTex.IsValid(), "BaseColor texture is invalid.");
							ASSERT(baseColorTex.GetFormat() == TEX_FORMAT_RGBA8_UNORM, "BaseColor must be RGBA8_UNORM for inline alpha SDF build.");

							// ---------------------------------------------------------------------
							// Inline: Build "Inner Depth" (R8) from baseColor alpha using EDT (O(W*H))
							// - outside: 0
							// - inside:  edge=0, deepest=1 (normalized PER connected component)
							// ---------------------------------------------------------------------
							auto BuildAlphaInnerMaskR8 = [](
								const Texture& srcRgba,
								float alphaThreshold) -> Texture
							{
								const uint32 W = srcRgba.GetWidth();
								const uint32 H = srcRgba.GetHeight();
								const uint8* src = srcRgba.GetData();

								ASSERT(W > 0 && H > 0, "Invalid src size.");
								ASSERT(srcRgba.GetMips().size() > 0, "No mips in src.");
								ASSERT(srcRgba.GetMips()[0].Data.size() >= size_t(W) * size_t(H) * 4ull, "Src data size mismatch.");

								const float INF = 1e20f;

								// 1D squared distance transform (Felzenszwalb/Huttenlocher)
								auto edt1d = [&](const std::vector<float>& f, int n, std::vector<float>& d)
								{
									std::vector<int> v(n);
									std::vector<float> z(n + 1);

									int k = 0;
									v[0] = 0;
									z[0] = -INF;
									z[1] = +INF;

									auto sq = [](float x) { return x * x; };

									for (int q = 1; q < n; ++q)
									{
										float s = 0.0f;
										for (;;)
										{
											const int vk = v[k];
											s = ((f[q] + sq(float(q))) - (f[vk] + sq(float(vk)))) / (2.0f * float(q - vk));
											if (s > z[k]) break;
											--k;
										}
										++k;
										v[k] = q;
										z[k] = s;
										z[k + 1] = +INF;
									}

									k = 0;
									for (int q = 0; q < n; ++q)
									{
										while (z[k + 1] < float(q)) ++k;
										const int vk = v[k];
										const float dx = float(q - vk);
										d[q] = dx * dx + f[vk];
									}
								};

								// 2D EDT: distance to nearest "feature"
								// binary: 0/1, targetOne==true => feature is 1 pixels, else feature is 0 pixels.
								auto edt2d = [&](const std::vector<uint8>& binary, bool targetOne) -> std::vector<float>
								{
									// f = 0 at feature pixels, INF elsewhere
									std::vector<float> f(W * H, INF);
									for (uint32 y = 0; y < H; ++y)
									{
										for (uint32 x = 0; x < W; ++x)
										{
											const uint8 b = binary[y * W + x];
											const bool isFeature = (b != 0) == targetOne;
											if (isFeature)
											{
												f[y * W + x] = 0.0f;
											}
										}
									}

									// pass 1: columns
									std::vector<float> g(W * H, INF);
									{
										std::vector<float> colF(H);
										std::vector<float> colD(H);
										for (uint32 x = 0; x < W; ++x)
										{
											for (uint32 y = 0; y < H; ++y)
											{
												colF[y] = f[y * W + x];
											}
											edt1d(colF, int(H), colD);
											for (uint32 y = 0; y < H; ++y)
											{
												g[y * W + x] = colD[y];
											}
										}
									}

									// pass 2: rows
									std::vector<float> d2(W * H, INF);
									{
										std::vector<float> rowF(W);
										std::vector<float> rowD(W);
										for (uint32 y = 0; y < H; ++y)
										{
											for (uint32 x = 0; x < W; ++x)
											{
												rowF[x] = g[y * W + x];
											}
											edt1d(rowF, int(W), rowD);
											for (uint32 x = 0; x < W; ++x)
											{
												d2[y * W + x] = rowD[x]; // squared distance
											}
										}
									}

									return d2;
								};

								// Build inside mask from alpha
								std::vector<uint8> inside(W * H, 0);
								for (uint32 y = 0; y < H; ++y)
								{
									for (uint32 x = 0; x < W; ++x)
									{
										const uint32 i = (y * W + x) * 4u;
										const float a = float(src[i + 3u]) * (1.0f / 255.0f);
										inside[y * W + x] = (a >= alphaThreshold) ? 1u : 0u;
									}
								}

								// Distance-to-edge for INSIDE pixels:
								// nearest OUTSIDE pixel distance => feature is outside==1 => inside==0
								std::vector<float> distToOutside2 = edt2d(inside, /*targetOne*/ false);

								// Connected component labeling on 'inside' to normalize per-leaf scale
								// Use 8-neighborhood for natural leaf connectivity.
								std::vector<int32> labels(W * H, -1);
								std::vector<float> distPx(W * H, 0.0f);

								for (uint32 idx = 0; idx < W * H; ++idx)
								{
									// For inside pixels, distToOutside2 is valid; outside can be ignored (keep 0).
									if (inside[idx] != 0)
									{
										distPx[idx] = std::sqrt(std::max(distToOutside2[idx], 0.0f));
									}
								}

								std::vector<float> compMaxDist;
								compMaxDist.reserve(256);

								auto InBounds = [&](int x, int y) -> bool
								{
									return (x >= 0 && y >= 0 && x < int(W) && y < int(H));
								};

								static const int kDirs8[8][2] =
								{
									{ -1, -1 }, { 0, -1 }, { 1, -1 },
									{ -1,  0 },           { 1,  0 },
									{ -1,  1 }, { 0,  1 }, { 1,  1 },
								};

								int32 compId = 0;

								std::vector<int32> queue;
								queue.reserve(4096);

								for (uint32 y0 = 0; y0 < H; ++y0)
								{
									for (uint32 x0 = 0; x0 < W; ++x0)
									{
										const uint32 idx0 = y0 * W + x0;
										if (inside[idx0] == 0) continue;
										if (labels[idx0] >= 0) continue;

										// BFS/DFS
										float maxD = 0.0f;

										labels[idx0] = compId;
										queue.clear();
										queue.push_back(int32(idx0));

										for (size_t qi = 0; qi < queue.size(); ++qi)
										{
											const uint32 idx = uint32(queue[qi]);
											maxD = std::max(maxD, distPx[idx]);

											const int x = int(idx % W);
											const int y = int(idx / W);

											for (int k = 0; k < 8; ++k)
											{
												const int nx = x + kDirs8[k][0];
												const int ny = y + kDirs8[k][1];
												if (!InBounds(nx, ny)) continue;

												const uint32 nidx = uint32(ny) * W + uint32(nx);
												if (inside[nidx] == 0) continue;
												if (labels[nidx] >= 0) continue;

												labels[nidx] = compId;
												queue.push_back(int32(nidx));
											}
										}

										compMaxDist.push_back(maxD);
										++compId;
									}
								}

								// Output R8 "inner depth"
								Texture out;
								out.SetFormat(TEX_FORMAT_R8_UNORM);
								out.GetMips().resize(1);
								out.GetMips()[0].Width = W;
								out.GetMips()[0].Height = H;
								out.GetMips()[0].Data.resize(size_t(W) * size_t(H));

								for (uint32 y = 0; y < H; ++y)
								{
									for (uint32 x = 0; x < W; ++x)
									{
										const uint32 idx = y * W + x;

										if (inside[idx] == 0)
										{
											out.GetMips()[0].Data[idx] = 0u; // outside fixed 0
											continue;
										}

										const int32 lid = labels[idx];
										ASSERT(lid >= 0 && lid < int32(compMaxDist.size()), "Invalid component label.");

										const float maxD = compMaxDist[size_t(lid)];
										// If a component is extremely thin (maxD==0), keep it 0 (edge-only)
										const float invMax = (maxD > 1e-6f) ? (1.0f / maxD) : 0.0f;

										// edge -> 0, deepest -> 1
										float v01 = std::clamp(distPx[idx] * invMax, 0.0f, 1.0f);

										out.GetMips()[0].Data[idx] = (uint8)std::clamp(v01 * 255.0f + 0.5f, 0.0f, 255.0f);
									}
								}

								return out;
							};

							// --- Build SDF in system memory ---
							const float alphaThreshold = 0.5f;
							Texture alphaSDF = BuildAlphaInnerMaskR8(baseColorTex, alphaThreshold);
							ASSERT(alphaSDF.IsValid(), "AlphaSDF build failed.");

							// --- Upload to GPU and bind ---
							TextureDesc desc = {};
							desc.Name = "Grass_AlphaSDF";
							desc.Type = RESOURCE_DIM_TEX_2D;
							desc.Width = alphaSDF.GetWidth();
							desc.Height = alphaSDF.GetHeight();
							desc.MipLevels = 1;
							desc.ArraySize = 1;
							desc.Format = TEX_FORMAT_R8_UNORM;
							desc.Usage = USAGE_IMMUTABLE;
							desc.BindFlags = BIND_SHADER_RESOURCE;

							TextureSubResData subRes = {};
							subRes.pData = alphaSDF.GetData();
							subRes.Stride = alphaSDF.GetWidth(); // R8: 1 byte/px

							TextureData initData = {};
							initData.NumSubresources = 1;
							initData.pSubResources = &subRes;

							static uint32 alphaInnerMaskIdx = 1;

							const uint64 alphaSDFResId = STRING_HASH(path + "_EdgeDist" + std::to_string(alphaInnerMaskIdx++));
							m_pRenderer->AddTexture(alphaSDFResId, desc, &initData);
							mat.SetTextureResource("g_EdgeDistance", alphaSDFResId);
						}

						grassMesh.AddLevel(std::move(grassMeshLevel), 1.0f);
					}
					// LOD1 : Cross-plane
					{
						AssetRef<AssimpAsset> grassCrossRef = m_pAssetManager->RegisterAsset<AssimpAsset>(path);
						const AssimpAsset& grassCrossAssimp = *m_pAssetManager->LoadBlocking(grassCrossRef);
						StaticMeshLevel grassCrossMeshLevel;
						BuildStaticMeshAsset(grassCrossAssimp, &grassCrossMeshLevel, {}, "GrassCrossPlane", nullptr, m_pAssetManager.get());
						uniform01(grassCrossMeshLevel);
						for (auto matId : grassCrossMeshLevel.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD1"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						grassMesh.AddLevel(std::move(grassCrossMeshLevel), 0.5f);
					}
					// LOD2 : Billboard
					{
						AssetRef<Texture> grassBillboardTexRef = m_pAssetManager->RegisterAsset<Texture>("C:/Dev/ShizenEngine/Assets/Grass/basic/clips/v1.png");
						StaticMeshLevel grassBiilboardMeshLevel = StaticMeshLevel::CreateBillboard(grassBillboardTexRef, "GrassBillboard", MATERIAL_BLEND_MODE_MASKED, { 1.0f, 1.0f }, { 0.5f, 0.0f });
						for (auto matId : grassBiilboardMeshLevel.GetMaterialSlots())
						{
							Material& mat = MaterialManager::GetInstance()->GetMaterial(matId);
							mat.SetBufferResource("g_GrassInstances", STRING_HASH("GrassInstanceBufferLOD2"));
							mat.SetBufferResource("g_SpeciesLodOffsets", STRING_HASH("Grass_SpeciesLodOffsets"));
							mat.SetCullMode(CULL_MODE_NONE);
						}
						grassMesh.AddLevel(std::move(grassBiilboardMeshLevel), 0.25f);
					}
					gd.pMesh = &m_pRenderer->CreateStaticMeshRenderData(grassMesh);
					m_pGrassSystem->AddGrassDesc(gd);
				};

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A.fbx");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A1.fbx");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A2.fbx");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A3.fbx");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/foliage_pack/SM_Grass_small_clump_A4.fbx");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Dandelion_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Dandelion_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Dandelion_03.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Dandelion_04.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Ezepnik_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Ezepnik_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Ezepnik_03.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Ezepnik_04.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Ezepnik_05.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Fern_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Fern_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Fern_03.gltf");

				addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Field_Assemble_01.gltf");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Field_Assemble_02.gltf");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Field_Assemble_03.gltf");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Field_Assemble_04.gltf");
				addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Field_Assemble_05.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Daisy_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Daisy_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Daisy_03.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Orange_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Orange_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Orange_03.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Flower_Orange_04.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Plantain_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Plantain_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Plantain_03.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Plantain_04.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Plantain_05.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Plantain_06.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_03.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_04.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_05.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_06.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Reed_07.gltf");

				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Short_01.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Short_02.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Short_03.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Short_04.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Short_05.gltf");
				//addGrass("C:/Dev/ShizenEngine/Assets/Grass/GrassFieldPack/SM_Plant_Grass_Short_06.gltf");
			}

			m_pInteractionSystem->InstallPasses(*m_pRenderer, *m_pTerrainSystem);
			m_pGrassSystem->InstallPasses(*m_pRenderer, *m_pRenderScene, *m_pInteractionSystem);
		}

		// ECS
		m_pEcs = std::make_unique<EcsWorld>();
		{
			ASSERT(m_pEcs, "ECS world is null.");
			shz::EcsWorld::CreateInfo eci = {};
			eci.FixedDeltaTime = 1.0f / 60.0f;
			eci.MaxFixedStepsPerFrame = 8;

			m_pEcs->Initialize(eci);
			ASSERT(m_pEcs->IsValid(), "EcsWorld is not initialized.");

			auto& ecs = m_pEcs->World();

			// Register components
			ecs.component<CName>();
			ecs.component<CTransform>();
			ecs.component<CMeshRenderer>();

			// New physics components
			ecs.component<CRigidbody>();
			ecs.component<CBoxCollider>();
			ecs.component<CSphereCollider>();
			ecs.component<CHeightFieldCollider>();
		}

		// PhysicsSystem (encapsulated Jolt)
		m_pPhysicsSystem = std::make_unique<PhysicsSystem>();
		{
			ASSERT(m_pPhysicsSystem, "PhysicsSystem is null.");

			PhysicsSystem::CreateInfo pci = {};
			pci.PhysicsCI.MaxBodies = 65536;
			pci.PhysicsCI.MaxBodyPairs = 65536;
			pci.PhysicsCI.MaxContactConstraints = 10240;
			pci.PhysicsCI.TempAllocatorSizeBytes = 16u * 1024u * 1024u;
			pci.PhysicsCI.Gravity = float3(0.0f, -9.81f, 0.0f);

			m_pPhysicsSystem->Initialize(pci);
		}

		// ECS: systems
		{
			// Install Physics <-> ECS systems (CreateBodies / PushTransform / WriteBack / OnRemove)
			m_pPhysicsSystem->InstallEcsSystems(*m_pEcs);

			// Update: Transform -> RenderScene sync
			{
				auto sys = m_pEcs->World().observer<CTransform, CMeshRenderer>("Render.SyncTransforms")
					.event(flecs::OnSet)
					.each([this](CTransform& tr, CMeshRenderer& mr)
						{
							if (!mr.RenderObjectHandle.IsValid())
								return;

							m_pRenderScene->UpdateObjectTransform(
								mr.RenderObjectHandle,
								Matrix4x4::TRS(tr.Position, tr.Rotation, tr.Scale));
						});
				m_pEcs->RegisterUpdateSystem(sys);
			}

			{
				auto sys = m_pEcs->World().system<CTransform, CRigidbody, CHeightFieldCollider>("World.UpdateInteractionField")
					.each([this](const CTransform& tr, const CRigidbody& rb, const CHeightFieldCollider& hf)
						{
							const PhysicsBodyHandle terrainBody = PhysicsBodyHandle{ rb.BodyHandle };
							const auto& events = m_pPhysicsSystem->GetContactEvents();

							// Prevent stamping same dynamic body multiple times in this terrain iteration.
							// Key: otherBody.Value (packed+1 handle value)
							std::unordered_set<uint32> stampedThisFrame;
							stampedThisFrame.reserve(64);

							for (const ContactEvent& contact : events)
							{
								const bool bInvolved =
									(contact.BodyA.Value == terrainBody.Value) ||
									(contact.BodyB.Value == terrainBody.Value);

								if (!bInvolved)
								{
									continue;
								}

								// Ignore removed for stamping (decay handles recovery).
								if (contact.Type == EContactEventType::Removed)
								{
									continue;
								}

								const PhysicsBodyHandle otherBody =
									(contact.BodyA.Value == terrainBody.Value) ? contact.BodyB : contact.BodyA;

								// Only dynamic bodies create interaction stamps.
								if (m_pPhysicsSystem->GetPhysics().GetBodyMotion(otherBody) != ERigidbodyType::Dynamic)
								{
									continue;
								}

								// Deduplicate per-frame per-body.
								if (!stampedThisFrame.emplace(otherBody.Value).second)
								{
									continue;
								}

								const float3 pWS = m_pPhysicsSystem->GetPhysics().GetBodyPosition(otherBody);

								hlsl::InteractionStamp stamp = {};
								stamp.CenterXZ = float2{ pWS.x, pWS.z };
								stamp.Radius = 1.5f;
								stamp.FalloffPower = 10.0f;
								stamp.TargetId = 0;

								if (contact.Type == EContactEventType::Added)
								{
									stamp.Strength = 1.0f;
									stamp.Flags = hlsl::INTERACTION_STAMP_MAX_BLEND; // still fine on Added
								}
								else // Persisted
								{
									// "Maintain" pressure: choose a slightly lower target than Added.
									stamp.Strength = 0.65f;
									stamp.Flags = hlsl::INTERACTION_STAMP_MAX_BLEND;
								}

								m_pRenderScene->AddInteractionStamp(stamp);
							}
						});

				m_pEcs->RegisterUpdateSystem(sys);
			}
		}

		// ViewFamily + Camera
		{
			m_ViewFamily.Views.clear();
			m_ViewFamily.Views.push_back({});

			//m_Camera.SetPos(float3(0.0f, m_pTerrainSystem->SampleWorldHeight(0.0f, 0.0f) + 1.0f, 0.0f));
			//m_Camera.SetRotation(0.0f, 0.0f);

			m_Camera.SetPos({ 26.2178898f, 359.614075f, -4.20217f });
			m_Camera.SetRotation(1.349999f, -0.70f);

			m_Camera.SetMoveSpeed(3.0f);
			m_Camera.SetSpeedUpScales(5.0f, 5.0f);
			m_Camera.SetRotationSpeed(0.01f);

			m_Camera.SetProjAttribs(
				0.1f,
				8000.0f,
				(float)m_Viewport.Width / (float)m_Viewport.Height,
				PI / 4.0f,
				SURFACE_TRANSFORM_IDENTITY);
		}

		// Light
		{
			m_GlobalLight.Direction = float3(0.4f, -1.0f, 0.3f);
			m_GlobalLight.Color = float3(1.0f, 1.0f, 1.0f);
			m_GlobalLight.Intensity = 2.0f;
		}
		m_GlobalLightHandle = m_pRenderScene->AddLight(m_GlobalLight);
		ASSERT(m_GlobalLightHandle.IsValid(), "Failed to add global light.");

		// Build scene once (ECS-driven objects)
		BuildSceneOnce();

		// Fill first view immediately
		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);
	}

	void GrassViewer::Render()
	{
		bench::Timer timer;

		ASSERT(m_pRenderer, "Renderer is null.");
		ASSERT(m_pRenderScene, "RenderScene is null.");

		m_ViewFamily.FrameIndex++;

		m_pRenderer->BeginFrame();
		m_pRenderer->Render(*m_pRenderScene, m_ViewFamily);
		m_pRenderer->EndFrame();

		m_RenderMs = timer.ElapsedMs();
		const double a = (double)std::clamp(m_TimingEmaAlpha, 0.0f, 1.0f);
		m_RenderMsEMA = a * m_RenderMs + (1.0 - a) * m_RenderMsEMA;
	}

	void GrassViewer::Update(double currTime, double elapsedTime, bool doUpdateUI)
	{
		bench::Timer timer;

		SampleBase::Update(currTime, elapsedTime, doUpdateUI);

		ASSERT(m_pRenderScene, "RenderScene is null.");

		const float dt = (float)elapsedTime;
		const float t = (float)currTime;

		m_Camera.Update(m_InputController, dt);

		m_ViewFamily.PrevDeltaTime = m_ViewFamily.DeltaTime;
		m_ViewFamily.PrevCurrentTime = m_ViewFamily.CurrentTime;

		m_ViewFamily.DeltaTime = dt;
		m_ViewFamily.CurrentTime = t;

		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);

		ASSERT(m_GlobalLightHandle.IsValid(), "GlobalLightHandle is invalid.");
		m_pRenderScene->UpdateLight(m_GlobalLightHandle, m_GlobalLight);

		ASSERT(m_pEcs->IsValid(), "ECS world is not valid.");
		if (m_pEcs->IsValid())
		{
			m_pEcs->Tick(dt);
		}

		m_pTerrainSystem->Update(*m_pRenderer, m_pRenderScene.get(), m_ViewFamily.Views[0]);

		// Update grass render constants
		m_pRenderer->UpdateBuffer<hlsl::GrassRenderConstants>(STRING_HASH("GrassRenderConstantsCB"), m_GrassSettings);

		m_UpdateMs = timer.ElapsedMs();
		const double a = (double)std::clamp(m_TimingEmaAlpha, 0.0f, 1.0f);
		m_UpdateMsEMA = a * m_UpdateMs + (1.0 - a) * m_UpdateMsEMA;
	}

	void GrassViewer::ReleaseSwapChainBuffers()
	{
		SampleBase::ReleaseSwapChainBuffers();
		m_pRenderer->ReleaseSwapChainBuffers();
	}

	void GrassViewer::WindowResize(uint32 width, uint32 height)
	{
		SampleBase::WindowResize(width, height);

		m_Viewport.Width = std::max(1u, width);
		m_Viewport.Height = std::max(1u, height);

		m_Camera.SetProjAttribs(
			m_Camera.GetProjAttribs().NearClipPlane,
			m_Camera.GetProjAttribs().FarClipPlane,
			(float)m_Viewport.Width / (float)m_Viewport.Height,
			m_Camera.GetProjAttribs().FOV,
			SURFACE_TRANSFORM_IDENTITY);

		m_pRenderer->OnResize(m_Viewport.Width, m_Viewport.Height);

		updatePrimaryView(m_ViewFamily, m_Viewport, m_Camera);
	}

	void GrassViewer::UpdateUI()
	{
		ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);

		if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::gizmo3D("##LightDirection", m_GlobalLight.Direction, ImGui::GetTextLineHeight() * 10);
			ImGui::ColorEdit3("##LightColor", reinterpret_cast<float*>(&m_GlobalLight.Color));
			ImGui::SliderFloat("Intensity", &m_GlobalLight.Intensity, 0.01f, 10.0f);

			float currExposure = m_pRenderScene->GetExposure();
			ImGui::SliderFloat("Exposure", &currExposure, 0.1f, 10.0f);
			m_pRenderScene->SetExposure(currExposure);

			ImGui::Separator();
			ImGui::TextDisabled("FPS: %.1f", ImGui::GetIO().Framerate);

			ImGui::Separator();
			ImGui::Text("CPU Timings (ms)");
			ImGui::Text("Update: %.3f (EMA %.3f)", (float)m_UpdateMs, (float)m_UpdateMsEMA);
			ImGui::Text("Render:  %.3f (EMA %.3f)", (float)m_RenderMs, (float)m_RenderMsEMA);
			ImGui::Text("Frame:   %.3f (EMA %.3f)",
				(float)(m_UpdateMs + m_RenderMs),
				(float)(m_UpdateMsEMA + m_RenderMsEMA));

			ImGui::SliderFloat("Timing EMA Alpha", &m_TimingEmaAlpha, 0.01f, 0.5f, "%.3f");

			ImGui::Separator();

			float prevSpeed = m_Speed;
			if (ImGui::DragFloat("Speed", &m_Speed, 0.05f, 0.01f, 100.0f, "%.3f"))
			{
				if (m_Speed < 0.01f) m_Speed = 0.01f;

				if (m_Speed != prevSpeed)
				{
					m_Camera.SetSpeedUpScales(m_Speed, 1.0f);
				}
			}

			// ---------------------------------------------------------------------
			// Grass Settings UI
			// ---------------------------------------------------------------------
			ImGui::Separator();
			ImGui::Text("Grass");

			// 1) Initialize defaults ONCE (do not override every frame)
			static bool s_InitGrassDefaults = false;
			if (!s_InitGrassDefaults)
			{
				s_InitGrassDefaults = true;

				// Wind (reasonable outdoor baseline)
				m_GrassSettings.WindDirXZ = float2{ 0.80f, 0.60f }.Normalized();
				m_GrassSettings.WindStrength = 0.35f;      // 0.2~0.6 typical
				m_GrassSettings.WindSpeed = 2.0f;          // 1~4 typical
				m_GrassSettings.WindFreq = 2.8f;           // 1.5~6 typical
				m_GrassSettings.WindGust = 0.35f;          // 0~1
				m_GrassSettings.MaxBendAngle = 0.35f;      // radians-ish (0~1)

				// Interaction
				m_GrassSettings.InteractionBendAngle = 1.0f;
				m_GrassSettings.InteractionSink = 0.05f;
				m_GrassSettings.InteractionWindFade = 0.95f;

				// Erosion (start subtle)
				m_GrassSettings.ErosionStrength = 0.0f;   // 0..1 progress
				m_GrassSettings.ErosionNoiseScale = 16.0f; // 8..48 typical
				m_GrassSettings.ErosionSmoothness = 0.75f;
				m_GrassSettings.ErosionMaxDist = 0.5f;

				// Drying look (foliage-appropriate warm yellow-brown)
				m_GrassSettings.DryTint = float3{ 0.70f, 0.56f, 0.28f };
				m_GrassSettings.DrySaturationReduct = 0.55f; // 0..1
				m_GrassSettings.DryDarken = 0.20f;           // 0..0.6
				m_GrassSettings.DryRoughness = 0.35f;        // 0..1
			}

			// Optional: Reset button
			if (ImGui::Button("Reset Grass Defaults"))
			{
				s_InitGrassDefaults = false;
			}

			// 2) Wind
			if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// Direction as 2D vector (normalize on edit)
				float dir[2] = { m_GrassSettings.WindDirXZ.x, m_GrassSettings.WindDirXZ.y };
				if (ImGui::DragFloat2("Dir XZ", dir, 0.01f, -1.0f, 1.0f, "%.3f"))
				{
					float2 d = float2{ dir[0], dir[1] };
					const float len2 = d.x * d.x + d.y * d.y;
					if (len2 < 1e-6f) d = float2{ 1.0f, 0.0f };
					m_GrassSettings.WindDirXZ = d.Normalized();
				}

				ImGui::SliderFloat("Strength", &m_GrassSettings.WindStrength, 0.0f, 1.5f, "%.3f");
				ImGui::SliderFloat("Speed", &m_GrassSettings.WindSpeed, 0.0f, 8.0f, "%.3f");
				ImGui::SliderFloat("Frequency", &m_GrassSettings.WindFreq, 0.5f, 10.0f, "%.3f");
				ImGui::SliderFloat("Gust", &m_GrassSettings.WindGust, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Max Bend", &m_GrassSettings.MaxBendAngle, 0.0f, 1.2f, "%.3f");

				ImGui::TextDisabled("Tip: Strength 0.2~0.6, Speed 1~4, Freq 2~6 is a good baseline.");
			}

			// 3) Interaction
			if (ImGui::CollapsingHeader("Interaction", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::SliderFloat("Bend Angle", &m_GrassSettings.InteractionBendAngle, 0.0f, 2.0f, "%.3f");
				ImGui::SliderFloat("Sink", &m_GrassSettings.InteractionSink, 0.0f, 0.20f, "%.3f");
				ImGui::SliderFloat("Wind Fade", &m_GrassSettings.InteractionWindFade, 0.0f, 1.0f, "%.3f");
			}

			// 4) Erosion + Drying
			if (ImGui::CollapsingHeader("Erosion / Drying", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::SliderFloat("Erosion Strength", &m_GrassSettings.ErosionStrength, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Noise Scale", &m_GrassSettings.ErosionNoiseScale, 2.0f, 64.0f, "%.2f");
				ImGui::SliderFloat("Smoothness", &m_GrassSettings.ErosionSmoothness, 0.01f, 1.0f, "%.3f");
				ImGui::SliderFloat("MaxDist", &m_GrassSettings.ErosionMaxDist, 0.01f, 1.0f, "%.3f");

				ImGui::Separator();

				float tint[3] = { m_GrassSettings.DryTint.x, m_GrassSettings.DryTint.y, m_GrassSettings.DryTint.z };
				if (ImGui::ColorEdit3("Dry Tint", tint))
				{
					// avoid (0,0,0) which kills the albedo
					m_GrassSettings.DryTint = float3{ std::max(tint[0], 0.01f), std::max(tint[1], 0.01f), std::max(tint[2], 0.01f) };
				}

				ImGui::SliderFloat("Saturation Reduce", &m_GrassSettings.DrySaturationReduct, 0.0f, 1.0f, "%.3f");
				ImGui::SliderFloat("Darken", &m_GrassSettings.DryDarken, 0.0f, 0.8f, "%.3f");
				ImGui::SliderFloat("Roughness Add", &m_GrassSettings.DryRoughness, 0.0f, 1.0f, "%.3f");

				ImGui::TextDisabled("Suggested: Smoothness 0.04~0.10, NoiseScale 10~24.");
				ImGui::TextDisabled("DryTint: warm yellow-brown, SatReduce 0.4~0.7, Darken 0.15~0.35.");
			}
		}

		ImGui::End();
	}

	// ------------------------------------------------------------
	// Scene build (same behavior, but physics now via ECS components)
	// ------------------------------------------------------------

	void GrassViewer::BuildSceneOnce()
	{
		ASSERT(m_pAssetManager && m_pRenderer && m_pRenderScene && m_pEcs && m_pPhysicsSystem, "Subsystem missing.");

		auto& ecs = m_pEcs->World();

		// -------------------------------------------------------------------------
		// Helpers (lambdas)
		// -------------------------------------------------------------------------
		auto loadTexture = [&](const std::string& path) -> const Texture&
		{
			AssetRef<Texture> ref = m_pAssetManager->RegisterAsset<Texture>(path);
			return *m_pAssetManager->LoadBlocking(ref);
		};

		auto getPixelStrideBytes = [&](const Texture& tex) -> uint32
		{
			const TEXTURE_FORMAT fmt = tex.GetFormat();
			const TextureFormatAttribs& a = GetTextureFormatAttribs(fmt);

			// Diligent: ComponentSize = bytes per component, NumComponents = channels
			const uint32 compSize = a.ComponentSize;
			const uint32 numComp = (a.NumComponents > 0) ? a.NumComponents : 1;
			return compSize * numComp;
		};

		auto getMip0 = [&](const Texture& tex) -> const TextureMip&
		{
			ASSERT(!tex.GetMips().empty(), "Texture has no mips.");
			return tex.GetMips()[0];
		};

		auto valueToBucket = [&](uint8 value) -> uint32
		{
			// 0: >200, 1: >=150, 2: >=100, 3: >=50, 4: >=1, else: skip
			if (value > 200) return 0;
			if (value >= 150) return 1;
			if (value >= 100) return 2;
			if (value >= 50)  return 3;
			if (value >= 1)   return 4;
			return 999;
		};

		enum class ETreeSize : uint8
		{
			Large,
			Big,
			Medium,
			Small,
			Sapling,
			Count
		};

		auto loadTree = [&](const std::string& folderName) -> const StaticMeshRenderData*
		{
			std::vector<AssetRef<AssimpAsset>> treeMeshRefs(4);
			for (uint lod = 0; lod < 4; ++lod)
			{
				std::string path = std::string("C:/Dev/ShizenEngine/Assets/Tree/pine_trees/") + folderName + "/lod" + std::to_string(lod) + ".fbx";
				treeMeshRefs[lod] = m_pAssetManager->RegisterAsset<AssimpAsset>(path);
			}
			return &m_pRenderer->CreateStaticMeshRenderData(treeMeshRefs);
		};

		auto pickSizeByBucket = [&](uint32 bucket, std::mt19937& rng) -> ETreeSize
		{
			std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
			const float r = dist01(rng);

			switch (bucket)
			{
			case 0:
				// Large 25% / Big 30% / Medium 25% / Small 15% / Sapling 5%
				if (r < 0.25f) return ETreeSize::Large;
				if (r < 0.55f) return ETreeSize::Big;
				if (r < 0.80f) return ETreeSize::Medium;
				if (r < 0.95f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 1:
				// Large 10% / Big 25% / Medium 40% / Small 20% / Sapling 5%
				if (r < 0.10f) return ETreeSize::Large;
				if (r < 0.35f) return ETreeSize::Big;
				if (r < 0.75f) return ETreeSize::Medium;
				if (r < 0.95f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 2:
				// Large 5% / Big 10% / Medium 50% / Small 25% / Sapling 10%
				if (r < 0.05f) return ETreeSize::Large;
				if (r < 0.15f) return ETreeSize::Big;
				if (r < 0.65f) return ETreeSize::Medium;
				if (r < 0.90f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 3:
				// Large 5% / Big 5% / Medium 25% / Small 50% / Sapling 15%
				if (r < 0.05f) return ETreeSize::Large;
				if (r < 0.10f) return ETreeSize::Big;
				if (r < 0.35f) return ETreeSize::Medium;
				if (r < 0.85f) return ETreeSize::Small;
				return ETreeSize::Sapling;

			case 4:
				// Large 0% / Big 5% / Medium 15% / Small 30% / Sapling 50%
				if (r < 0.05f) return ETreeSize::Big;
				if (r < 0.20f) return ETreeSize::Medium;
				if (r < 0.50f) return ETreeSize::Small;
				return ETreeSize::Sapling;
			default:
				ASSERT(false, "Invalid bucket value.");
				return ETreeSize::Medium;
			}
		};

		auto pickScaleBySize = [&](ETreeSize size, std::mt19937& rng) -> float
		{
			switch (size)
			{
			case ETreeSize::Large: { std::uniform_real_distribution<float> d(0.90f, 1.15f); return d(rng); }
			case ETreeSize::Big: { std::uniform_real_distribution<float> d(0.90f, 1.12f); return d(rng); }
			case ETreeSize::Medium: { std::uniform_real_distribution<float> d(0.90f, 1.10f); return d(rng); }
			case ETreeSize::Small: { std::uniform_real_distribution<float> d(0.90f, 1.08f); return d(rng); }
			case ETreeSize::Sapling: { std::uniform_real_distribution<float> d(0.90f, 1.06f); return d(rng); }
			default: return 1.0f;
			}
		};

		auto addRenderOnlyStaticMeshEntity = [&](
			const char* name,
			const StaticMeshRenderData& meshRD,
			const float3& pos,
			const float3& rot,
			const float3& scl,
			bool bCastShadow) -> flecs::entity
		{
			flecs::entity e = ecs.entity();
			e.set<CName>({ name });

			CTransform tr = {};
			tr.Position = pos;
			tr.Rotation = rot;
			tr.Scale = scl;
			e.set<CTransform>(tr);

			CMeshRenderer mr = {};
			mr.MeshRef = {};
			mr.bCastShadow = bCastShadow;
			mr.RenderObjectHandle = m_pRenderScene->AddObject(
				meshRD,
				Matrix4x4::TRS(tr.Position, tr.Rotation, tr.Scale),
				bCastShadow);
			e.set<CMeshRenderer>(mr);

			return e;
		};

		// -------------------------------------------------------------------------
		// Physics Terrain: HeightFieldCollider + Static Rigidbody
		// -------------------------------------------------------------------------
		{
			const uint32 W = m_pTerrainSystem->GetWidth();
			const uint32 H = m_pTerrainSystem->GetHeight();

			ASSERT(W == H, "HeightField collider: width/height must be equal (square) for your current pipeline.");

			std::vector<float> samples;
			m_pTerrainSystem->BuildPhysicsHeightSamples(samples);
			ASSERT(samples.size() == size_t(W) * size_t(H), "HeightField samples size mismatch.");

			const float spacing = m_pTerrainSystem->GetWorldSpacing();
			const float worldOriginX = m_pTerrainSystem->GetWorldOriginX();
			const float worldOriginZ = m_pTerrainSystem->GetWorldOriginZ();

			flecs::entity e = ecs.entity();
			e.set<CName>({ "TerrainPhysics" });

			CTransform tr = {};
			tr.Position = { worldOriginX, 0.0f, worldOriginZ };
			tr.Rotation = { 0.0f, 0.0f, 0.0f };
			tr.Scale = { 1.0f, 1.0f, 1.0f };
			e.set<CTransform>(tr);

			CRigidbody rb = {};
			rb.BodyType = ERigidbodyType::Static;
			rb.Layer = 0; // NonMoving
			rb.bEnableGravity = false;
			rb.bStartActive = false;
			e.set<CRigidbody>(rb);

			CHeightFieldCollider hf = {};
			hf.Width = W;
			hf.Height = H;
			hf.CellSizeX = spacing;
			hf.CellSizeZ = spacing;
			hf.HeightScale = 1.0f;   // samples already in world meters
			hf.HeightOffset = 0.0f;
			hf.Heights = std::move(samples);
			e.set<CHeightFieldCollider>(hf);
		}

		// -------------------------------------------------------------------------
		// Trees: placement texture driven, LOD2 fixed, size-by-bucket + variant uniform
		// -------------------------------------------------------------------------
		{
			const std::string treePlacementTexPath = "C:/Dev/ShizenEngine/Assets/Terrain/Chroma/trees.png";
			const Texture& treePlacementTex = loadTexture(treePlacementTexPath);

			const TextureMip& mip0 = getMip0(treePlacementTex);
			const uint32 pixelStrideBytes = getPixelStrideBytes(treePlacementTex);

			// Analyze counts (optional)
			{
				uint32 c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;

				for (uint32 y = 0; y < mip0.Height; ++y)
				{
					for (uint32 x = 0; x < mip0.Width; ++x)
					{
						const uint32 idx = (y * mip0.Width + x) * pixelStrideBytes;
						const uint8 value = mip0.Data[idx + 0];

						if (value > 200) c0++;
						else if (value >= 150) c1++;
						else if (value >= 100) c2++;
						else if (value >= 50)  c3++;
						else if (value >= 1)   c4++;
					}
				}

				std::cout << "Tree placement tex analysis (pixel count per threshold):\n";
				std::cout << " >200 (bucket0): " << c0 << "\n";
				std::cout << " >=150(bucket1): " << c1 << "\n";
				std::cout << " >=100(bucket2): " << c2 << "\n";
				std::cout << " >=50 (bucket3): " << c3 << "\n";
				std::cout << " >=1  (bucket4): " << c4 << "\n";
			}

			const StaticMeshRenderData* sizeMeshes[(int)ETreeSize::Count] = {};
			sizeMeshes[(int)ETreeSize::Large] = loadTree("large_1");
			sizeMeshes[(int)ETreeSize::Big] = loadTree("big_1");
			sizeMeshes[(int)ETreeSize::Medium] = loadTree("medium_1");
			sizeMeshes[(int)ETreeSize::Small] = loadTree("small_1");
			sizeMeshes[(int)ETreeSize::Sapling] = loadTree("sapling_1");

			constexpr float4 SPAWN_RANGE = { -5000.0f, -5000.0f, 5000.0f, 5000.0f };

			float yOffset = 0.0f;
			std::mt19937 rng(1337);

			std::uniform_real_distribution<float> distYaw(0.0f, TWO_PI);
			std::uniform_real_distribution<float> distJitter(-0.45f, 0.45f);
			std::uniform_int_distribution<int>    distVariant(0, 2);

			uint32 spawned = 0;

			for (uint32 y = 0; y < mip0.Height; ++y)
			{
				for (uint32 x = 0; x < mip0.Width; ++x)
				{
					const uint32 idx = (y * mip0.Width + x) * pixelStrideBytes;

					// Use first channel as grayscale
					const uint8 value = mip0.Data[idx + 0];
					if (value < 1)
					{
						continue;
					}

					const uint32 bucket = valueToBucket(value);
					const ETreeSize size = pickSizeByBucket(bucket, rng);
					const StaticMeshRenderData* pMeshRD = sizeMeshes[(int)size];

					float2 domainUV = float2((float)x / (float)mip0.Width, (float)y / (float)mip0.Height);
					float2 worldXZ = m_pTerrainSystem->DomainUVToWorldXZ(domainUV);

					const float worldY = m_pTerrainSystem->SampleWorldHeight(worldXZ.x, worldXZ.y) + yOffset;

					const float yaw = distYaw(rng);
					const float scl = pickScaleBySize(size, rng);

					addRenderOnlyStaticMeshEntity(
						"Tree",
						*pMeshRD,
						{ worldXZ.x, worldY, worldXZ.y },
						{ 0.0f, yaw, 0.0f },
						{ scl, scl, scl },
						true);

					++spawned;
				}
			}

			std::cout << "Spawned trees: " << spawned << "\n";
		}
	}
} // namespace shz
