# Harness di SingleStepTests. È un eseguibile a parte e non un test di CTest, perché ha
# bisogno dei dati scaricati con scripts/fetch_ssts.sh (circa 2,8 GB decompressi).
add_executable(ssts_runner ssts_runner.cpp)
target_link_libraries(ssts_runner PRIVATE emu_core nlohmann_json::nlohmann_json)
emu_set_options(ssts_runner)
