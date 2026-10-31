# Dipendenze esterne con versione fissata: archivio della release + hash SHA256.
# Un tag git si può spostare, l'hash no.

include(FetchContent)

FetchContent_Declare(Catch2
  URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
  URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
  SYSTEM)   # header trattati come di sistema: niente nostri warning su Catch2

# nlohmann/json per l'harness di SingleStepTests. L'archivio json.tar.xz della release
# contiene solo gli header e il CMake, non i test e la documentazione.
FetchContent_Declare(nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  SYSTEM)

FetchContent_MakeAvailable(Catch2 nlohmann_json)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)   # per include(Catch)
