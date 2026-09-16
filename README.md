# OpenRipper

**OpenRipper** is an open-source research toolkit designed for studying rendering architectures, graphics pipelines, and cross-platform API translation mechanisms. This project is developed strictly for academic, educational, and analytical purposes.

---

⚖️ Disclaimer & Research Manifesto

> **IMPORTANT NOTICE:** This project is built by developers, for developers and computer graphics researchers.

* **Academic Focus:** The project serves as an experimental platform for visualizing and inspecting modern graphics APIs (Vulkan, DirectX, OpenGL) and investigating rendering pipeline optimization techniques.
* **Anti-Piracy Commitment:** OpenRipper is **not designed** for unauthorized copying, distribution, DRM circumvention, or any infringement of intellectual property rights. The author strictly opposes digital piracy in all its forms.
* **Zero Proprietary Assets:** This repository **does not contain** and will never host copyrighted assets (textures, meshes, shaders, geometry, or game binaries).
* **User Responsibility:** Users bear sole responsibility for ensuring their workflows comply with end-user license agreements (EULAs) and applicable copyright laws within their local test environments.

---

📊 API & Environment Support Status

| Graphics API / Environment | Current Status |
| :--- | :---: |
| **Vulkan (Legacy/RPCS3)** | 🟢 Supported |
| **DirectX 9 — 12** | 🟡 In Development |
| **OpenGL** | 🔴 Planned |
| **GUI Loader** | 🟢 Ready |

---

🛠️ Feature & Export Capabilities

| Feature / Export Format | Current Status | Notes |
| :--- | :---: | :--- |
| **Raw Mesh Export (.obj)** | 🟢 Supported | Basic geometry extraction for analysis |
| **Texture Dumping** | 🟡 Planned | Texture extraction is currently not implemented |
| **Texture Export Support** | 🔴 Not Supported | Planned for upcoming releases |
| **Modern Scene Export (.gltf / .glb)** | 🟡 Planned | Future update will bring modern format for extracting meshes for analysis |

> **Note on current capabilities:** At this stage, OpenRipper does not support texture extraction. The engine only processes raw geometry exported into standard OBJ files. Full texture dumping alongside modern container formats (GLTF/GLB) will be introduced in future iterations.

---
