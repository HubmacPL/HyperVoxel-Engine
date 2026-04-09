# VoxelEngine — Architektura i dokumentacja

Klon Minecrafta zoptymalizowany pod kątem renderowania milionów bloków przy 60+ FPS.
C++20 · OpenGL 4.5 · GLFW · GLEW · GLM · vcpkg

---

## 1. Struktura projektu

```
minecraft_clone/
├── CMakeLists.txt          # system budowania z integracją vcpkg
├── vcpkg.json              # manifest zależności
├── .vscode/
│   ├── launch.json         # konfiguracja debuggera
│   └── tasks.json          # zadania build (Debug / Release)
├── include/                # nagłówki (.h)
│   ├── BlockRegistry.h     # definicje bloków + mapa tekstur atlasu
│   ├── Chunk.h             # dane chunka, paleta, mesh, stany
│   ├── ChunkMesher.h       # algorytm face-culling + AO
│   ├── ChunkManager.h      # asynchroniczny pipeline gen → mesh → upload
│   ├── World.h             # mapa chunków, custom hash ivec2
│   ├── TerrainGenerator.h  # Perlin noise, biomy, jaskinie, drzewa
│   ├── Renderer.h          # frustum culling, batched draw calls
│   ├── Shader.h            # RAII wrapper na program GLSL
│   ├── Camera.h            # kamera FPS
│   ├── Physics.h           # AABB + swept collision + Player
│   ├── Texture.h           # stb_image loader (RAII)
│   ├── TextureAtlas.h      # wrapper atlasu 256×256
│   └── ThreadPool.h        # pula wątków z futures
├── src/                    # implementacje (.cpp)
│   ├── main.cpp            # Application + game loop
│   ├── BlockRegistry.cpp
│   ├── Chunk.cpp
│   ├── ChunkMesher.cpp     # ← KLUCZOWY PLIK OPTYMALIZACJI
│   ├── ChunkManager.cpp
│   ├── World.cpp
│   ├── TerrainGenerator.cpp
│   ├── Renderer.cpp
│   ├── Shader.cpp
│   ├── Camera.cpp
│   ├── Physics.cpp         # zawiera też Player
│   ├── Texture.cpp
│   └── ThreadPool.cpp
└── shaders/
    ├── chunk.vert          # packed vertex unpacking + AO + fog
    ├── chunk.frag          # atlas sampling + directional light
    ├── water.vert          # wave displacement
    └── water.frag          # scrolling UV + alpha blend
```

---

## 2. Szybki start (Windows + Visual Studio / MSVC)

```powershell
# 1. Zainstaluj vcpkg (jeśli nie masz)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\vcpkg"

# 2. Sklonuj / otwórz projekt
cd minecraft_clone

# 3. Skonfiguruj i zbuduj (Debug)
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --parallel

# 4. Uruchom
.\build\Release\MinecraftClone.exe
```

**W VSCode:** `Ctrl+Shift+B` → wybierz `cmake-build-release`.

> **Uwaga:** Pobierz `stb_image.h` z https://github.com/nothings/stb i umieść
> w `include/`. Używany do ładowania tekstur PNG atlasu.

---

## 3. Kluczowe techniki optymalizacji — szczegółowy opis

### 3.1 Face Culling (ChunkMesher.cpp)

Najbardziej fundamentalna optymalizacja: **nie generuj ściany, której nie można zobaczyć**.

```
Dla każdego bloku B na pozycji (x,y,z):
  Jeśli B == Air → pomiń (fast path, ~60% bloków)
  Dla każdej z 6 ścian:
    Sprawdź sąsiadujący blok N
    Jeśli N jest nieprzezroczysty → pomiń ścianę (cull)
    W przeciwnym razie → wygeneruj quad
```

**Efekt:** Typowy chunk ma ~100 000 bloków, ale tylko ~2 000–8 000 widocznych ścian.
Redukcja geometrii: **95–98%**.

Kolejność iteracji w pętli wewnętrznej jest kluczowa dla cache:
```
for y → for z → for x   // matches layout: [y*W*D + z*W + x]
```
Dzięki temu każde odwołanie do bloku (x, y, z) jest sekwencyjne w pamięci.

---

### 3.2 Data-Oriented Design — układ danych bloku

```cpp
// Chunk::rawData_ — GORĄCA ŚCIEŻKA dla meshera
std::array<uint16_t, CHUNK_VOL> rawData_;  // 128 KB, ciągłe w pamięci

// Indeks: Y outermost zapewnia, że pionowy skan kolumny = sekwencyjny odczyt
int blockIndex(int x, int y, int z) {
    return y * (CHUNK_W * CHUNK_D) + z * CHUNK_W + x;
}
```

**Dlaczego `uint16_t`?**  
- `uint8_t` → max 256 typów (wystarczało na vanilla Minecraft)  
- `uint16_t` → 65 536 typów, nadal mieści się w 2 liniach cache na 8 bloków  
- `uint32_t` → podwajamy zużycie RAM bez korzyści (256 KB/chunk)

**Chunk Palette (ChunkPalette)** — zimna ścieżka (save/load, RAM):
```
≤256 unique types → uint8 indices + palette[] → 4 KB zamiast 128 KB (96% mniej RAM)
>256 types        → uint16 direct fallback
```
Przeciętny chunk w terenie ma 4–12 unikalnych typów → **kompresja 32:1 w paletycie**.

---

### 3.3 Vertex Packing — minimalizacja przepustowości VRAM

```cpp
struct ChunkVertex {
    uint8_t x, y_lo, y_hi, z;  // pozycja: 4 bajty (0-15, 0-255, 0-15)
    uint8_t u, v;               // UV wewnątrz kafelka: 2 bajty (0 lub 1)
    uint8_t tileX, tileY;       // kafelek atlasu: 2 bajty (0-15)
    uint8_t ao;                 // ambient occlusion: 1 bajt (0-3)
    uint8_t normal;             // kierunek ściany: 1 bajt (0-5)
    uint8_t pad[2];             // padding do 12 bajtów
};  // sizeof = 12 bajtów
```

Tradycyjny vertex (float pos + float UV + float normal) = **32 bajty**.  
Nasz packed vertex = **12 bajtów** → **2.7× mniej VRAM bandwidth**.

Przy 1M widocznych ścian × 4 wierzchołki = 4M wierzchołków:
- Standardowo: **128 MB** danych werteksów
- Packed: **48 MB** — różnica odczuwalna na GPU z wąskim memory bus

---

### 3.4 Wielowątkowość — pipeline bez stutteringu

```
Main Thread               genPool_ (N-1 wątki)    meshPool_ (N/2 wątki)
────────────────────────────────────────────────────────────────────────
World::update()
  │
  ├─ loadChunksAround()   ← tworzy Chunk(state=Empty)
  │
  └─ ChunkManager::tick()
       │
       ├─ [state=Empty]  ──► scheduleGeneration() ──► TerrainGenerator::generate()
       │                                               (czysto CPU, brak GL, thread-safe)
       │
       ├─ [state=Generated, neighbours OK] ──► scheduleMeshing() ──► ChunkMesher::buildMesh()
       │                                                              (czysto CPU, brak GL)
       │
       └─ uploadReadyMeshes(max=4/frame)
            └─ chunk.mesh().uploadToGPU()   ← TYLKO TUTAJ wywołania GL
                                              (max 4 chunki/klatkę = ~0.5ms overhead)
```

**Dlaczego tylko 4 uploady/klatkę?**  
`glBufferData()` blokuje szynę PCI-e. Upload ~5K werteksów ≈ 0.1 ms.  
4 uploady = 0.4 ms → mieści się w budżecie 16.7 ms klatki.

**Budżet wątków:**
```cpp
genPool_  = hardware_concurrency() - 1  // 7 wątków na 8-rdzeniowym CPU
meshPool_ = hardware_concurrency() / 2  // 4 wątki (mesh wymaga danych sąsiadów)
```

---

### 3.5 Ambient Occlusion oparte na werteksach

AO Minecrafta (technika z oryginalnej gry, popularna dzięki pracy Mikaela Christofilosa):

```
Dla każdego rogu quada (4 rogi × 6 ścian = 24 próbki/blok):
  Sprawdź 3 sąsiadujące bloki w tym rogu:
    side1  = blok wzdłuż osi U
    side2  = blok wzdłuż osi V  
    corner = blok po przekątnej

  AO = 0 (ciemny) jeśli side1 && side2
  AO = 3 - (side1 + side2 + corner)  w pozostałych przypadkach
```

Mapowanie AO na jasność:
```glsl
v_AO = 0.25 + aoRaw * 0.25;
// ao=0 → 0.25 (25% jasności — cień w rogu)
// ao=3 → 1.00 (100% jasności — brak okluzji)
```

**Trick z przekątną quada** (eliminacja artefaktu anizotropii):
```cpp
// Bez tego quady wyglądają jak szachownica
if (ao[0] + ao[3] > ao[1] + ao[2])
    // standardowa przekątna: 0-1-2, 0-2-3
else
    // odwrócona przekątna: 1-2-3, 0-1-3
```

---

### 3.6 Frustum Culling

```
Dla każdego chunka C:
  AABB = (origin, origin + (16, 256, 16))
  Sprawdź 6 płaszczyzn frustuma (wyznaczanych z macierzy VP)
  Jeśli AABB jest poza którąkolwiek płaszczyzną → pomiń chunk
```

Przy renderDistance=10, obszar ładowania = π×10² ≈ 314 chunków.
Frustum (FOV 70°) obejmuje ~30% → rysujemy ~95 chunków zamiast 314.
**Redukcja draw calls: ~70%.**

Sortowanie front-to-back przed renderingiem:  
GPU może odrzucić fragmenty zakryte przez wcześniejsze trójkąty (early-z).  
Przy gęstym terenie: **40–60% mniej fragmentów do procesowania**.

---

### 3.7 Batch Rendering — minimalizacja state changes

```
// ZŁE — jeden draw call na chunk to standard, ale możemy lepiej:
for (chunk : visible) {
    glBindTexture(...)   // zmiana stanu!
    glUseProgram(...)    // zmiana stanu!
    glDrawElements(...)
}

// DOBRE — wszystkie chunki, jeden shader, jeden bind atlasu:
glUseProgram(chunkShader);      // 1×
glBindTexture(atlas, 0);        // 1×
for (chunk : sorted_visible) {
    glUniform3fv(chunkOrigin);  // 1 uniform/chunk (tani)
    glBindVertexArray(mesh.vao);
    glDrawElements(...);        // 1 draw call/chunk
}
```

**Dalsze możliwe ulepszenie — Multi-Draw Indirect:**
```cpp
// Wszystkie widoczne chunki w JEDNYM wywołaniu GPU
glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                             drawCommands.data(),
                             visibleCount, 0);
```
Wymaga VBO z komendami rysowania na GPU — eliminuje pętle CPU całkowicie.

---

## 4. Schemat przepływu danych

```
  Ruch gracza
      │
      ▼
  World::update()
      │
      ├──► loadChunksAround()  →  new Chunk(Empty)
      │
      └──► ChunkManager::tick()
               │
         ┌─────┴──────────────────────────────────────┐
         │  Background Threads (genPool_)              │
         │  TerrainGenerator::generate(chunk)          │
         │  • Perlin fbm2D (6 oktaw)                   │
         │  • Biomy (temp/humidity → Biome enum)       │
         │  • fillColumn() → Grass/Dirt/Stone/Sand     │
         │  • Jaskinie (3D Perlin threshold 0.6)       │
         │  • Drzewa (RNG per chunk)                   │
         │  chunk.state = Generated                    │
         └─────────────────────────────────────────────┘
               │
         ┌─────┴──────────────────────────────────────┐
         │  Background Threads (meshPool_)             │
         │  ChunkMesher::buildMesh(ctx)                │
         │  • Iteracja Y→Z→X (cache-friendly)          │
         │  • Face culling (6 kierunków)               │
         │  • Cross-chunk boundary lookups             │
         │  • AO per vertex (3 próbki × 4 rogi)       │
         │  • emitFace() → ChunkVertex packed          │
         │  chunk.state = Ready                        │
         └─────────────────────────────────────────────┘
               │
         GL Thread (main)
         uploadReadyMeshes(max 4/frame)
         • glBufferData(VBO, vertices)
         • glBufferData(EBO, indices)
         • glVertexAttribIPointer(...)
         chunk.state = Uploaded
               │
               ▼
         Renderer::renderOpaquePass()
         • Frustum culling
         • Sort front-to-back
         • 1× glUseProgram, 1× glBindTexture
         • N× glDrawElements (N = widoczne chunki)
```

---

## 5. Sterowanie

| Klawisz | Akcja |
|---------|-------|
| `WSAD`  | ruch |
| `Spacja` | skok |
| `Mysz` | obrót kamery |
| `LPM` | niszczenie bloku |
| `PPM` | stawianie bloku (Kamień) |
| `F3` | tryb wireframe |
| `F4` | tryb latania (fly) |
| `Esc` | wyjście |

---

## 6. Metryki wydajności (szacowane, i7-9700K + RTX 2060)

| Parametr | Wartość |
|----------|---------|
| Render distance | 10 chunków |
| Łączne chunki w pamięci | ~314 |
| Widoczne chunki (frustum) | ~80–100 |
| Draw calls/klatkę | 80–100 |
| Werteksy na chunk (avg) | ~6 000 |
| RAM per chunk (raw) | 128 KB |
| RAM per chunk (paleta) | ~4–8 KB |
| Czas generacji chunka (BG) | ~2–8 ms |
| Czas budowania mesha (BG) | ~1–4 ms |
| GPU upload per chunk | ~0.1 ms |
| Docelowe FPS | **60–144+** |

---

## 7. Dalsze możliwe ulepszenia

### Greedy Meshing
Zamiast quad per ścianę → łącz sąsiadujące, identyczne ściany w większe prostokąty.
Redukcja geometrii: dodatkowe **60–80%** (zależnie od terenu).
Implementacja: dwuprzebiegowy algorytm "sweep" wzdłuż każdej osi.

### Level of Detail (LOD)
Odległe chunki renderuj niższą rozdzielczością (np. co 2. blok).
Technika: przechowuj mesh LOD0 (pełny) i LOD1 (uproszczony) osobno.

### Occlusion Culling
Nie renderuj chunków, które są całkowicie zasłonięte przez inne chunki.
Technika: Hierarchical Z-Buffer lub GPU occlusion queries.

### Multi-Draw Indirect (MDI)
```cpp
glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
                             gpuDrawCommands, visibleCount, 0);
```
Jeden call CPU → GPU przetwarza N chunków bez interwencji CPU.

### Persistent Mapped Buffers
```cpp
glBufferStorage(GL_ARRAY_BUFFER, size, nullptr,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
void* ptr = glMapBufferRange(...);
// Zapis bezpośrednio z CPU bez dodatkowego kopiowania
```

---

## 8. Zależności zewnętrzne

| Biblioteka | Źródło | Cel |
|-----------|--------|-----|
| GLFW 3.x | vcpkg | Okno, kontekst OpenGL, input |
| GLEW 2.x | vcpkg | Ładowanie rozszerzeń OpenGL |
| GLM 0.9.9 | vcpkg | Matematyka wektorów/macierzy |
| stb_image.h | github.com/nothings/stb | Ładowanie PNG (atlas tekstur) |

> stb_image.h nie jest w vcpkg — pobierz ręcznie i umieść w `include/`.
