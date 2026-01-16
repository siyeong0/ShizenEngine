# ShizenEngine

ShizenEngine is a **C++-based custom game and rendering engine** designed for **real-time environment rendering and interactive editing**.

The engine is built with a strong focus on large-scale natural environments such as **atmosphere, volumetric clouds, terrain, and vegetation**, aiming to bridge research-oriented rendering techniques with practical, extensible engine architecture.

---

## Goals

* 🌤 **Real-Time Environment Rendering**

  * Physically-based atmosphere scattering
  * Volumetric cloud rendering
  * Terrain rendering (heightmap, clipmap, virtual texturing)
  * Vegetation rendering (grass, trees, GPU instancing)

* 🛠 **Interactive Editing**

  * Real-time parameter tweaking
  * Strong coupling between rendering systems and tools
  * Editor-friendly engine structure

* ⚙ **Low-Level, Scalable Engine Architecture**

  * C++17-based implementation
  * Explicit and modular system boundaries

---

## Engine Architecture Overview

ShizenEngine uses a **clear top-level separation** between engine code, platform layers, tools, samples, and third-party dependencies. This layout is designed to scale from research prototypes to full applications while keeping dependencies explicit.

```
Root/
├─ Engine/               # Core engine implementation
│  ├─ Core/              # Fundamental engine systems
│  │  ├─ Common/         # Core types, utilities, macros
│  │  ├─ Math/           # Custom math library (SIMD-friendly)
│  │  ├─ Memory/         # Allocators and memory systems
│  │  ├─ Runtime/        # Rumtime features
│  │  └─ ...
│  │
│  ├─ RHI/               # Render Hardware Interface (API-agnostic)
│  ├─ RHI_D3DBase/       # Shared Direct3D backend code
│  ├─ RHI_D3D12/         # Direct3D 12 implementation
│  ├─ Renderer/          # High-level renderer
│  │
│  ├─ GraphicsArchiver/  # Render state / pipeline archiving
│  ├─ GraphicsTools/     # Runtime graphics helper utilities
│  ├─ GraphicsUtils/     # Low-level rendering utilities
│  ├─ ShaderTools/       # Shader compilation & reflection tools
│  ├─ AssetRuntime/      # Asset load/save and management
│  │
│  ├─ ImGui/             # Engine-integrated ImGui layer
│  ├─ Tools/             # Standalone tools
│  │  ├─ Image/          # Image Load/Save/Processing tools
│  │  └─ ...
│  └─ ...
│
├─ Platforms/            # Platform abstraction layer
│  ├─ Common/            # Platform-agnostic platform code
│  ├─ Win64/             # Windows 64-bit implementation
│  └─ ...
│
├─ Primitives/           # Shared public headers and primitive types
│
├─ Apps/
│  ├─ Editor
│  ├─ Viewer
│  └─ ...
│
├─ ThirdParty/           # External dependencies
│  ├─ imgui/             # ImGui 1.92.1 (tracked directly)
│  ├─ assimp/
│  ├─ spirv/
│  ├─ libjpeg/
│  ├─ libpng/
│  ├─ stb/
│  ├─ tiff/
│  └─ xxhash/
└─ README.md
```

---

## Technology Stack

* **Language**: C++20
* **Graphics APIs**:

  * Direct3D 12 (primary)
  * Vulkan (planned)
* **Shaders**: HLSL (DXC)
* **Build Systems**:
  * Visual Studio / MSBuild
* **Math Library**: Custom SIMD-enabled math library
* **UI / Tools**: ImGui 1.92.1, ImGuizmo

--

## Building (Windows)

### Requirements

* Windows 10 / 11
* Visual Studio 2019 or 2022
* Windows SDK
* DirectX 12–capable GPU

### Build Steps

```bash
git clone https://github.com/yourname/ShizenEngine.git
```

* Open `ShizenEngine.sln` in Visual Studio
* Build the solution

---

## References 

* [Unreal Engine](https://www.unrealengine.com)
* [Frostbite Engine](https://www.ea.com/frostbite)
* [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine)
* [Nebula Engine](https://chatgpt.com/c/69639386-3e60-8333-848c-ff1cfd15745e)
* [Megayuchi's D3D12Lecture](https://github.com/megayuchi/D3D12Lecture)
