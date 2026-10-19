file(REMOVE_RECURSE "${work}")
set(root "${work}/Հայ Рус portable")
file(MAKE_DIRECTORY "${root}/schemas" "${root}/модели/package")
file(COPY "${schemas}/config.schema.json" "${schemas}/model.schema.json" DESTINATION "${root}/schemas")
file(COPY "${runner}" DESTINATION "${root}/runners/stub")
file(COPY "${exe}" DESTINATION "${root}")
get_filename_component(host_name "${exe}" NAME)
file(WRITE "${root}/модели/package/մոդել.bin" "fixture")
file(WRITE "${root}/модели/package/голос.bin" "fixture")
file(WRITE "${root}/модели/package/model.json" [=[
{"$schema":"../../schemas/model.schema.json","schemaVersion":1,"id":"unicode","displayName":"Unicode","engine":"stub","languages":["en"],"files":{"model":"մոդել.bin","voice":"голос.bin"},"license":{"name":"test","url":"https://example.com"}}
]=])
file(READ "${config}" document)
string(JSON document SET "${document}" "$schema" "\"schemas/config.schema.json\"")
string(JSON document SET "${document}" modelRegistry directories "[\"модели\"]")
string(JSON document SET "${document}" profiles fast model "\"unicode\"")
string(JSON document SET "${document}" languageDefaults en "\"fast\"")
string(JSON document SET "${document}" startup "{\"launchOnLogin\":false}")
file(WRITE "${root}/настройки.json" "${document}")
execute_process(COMMAND "${root}/${host_name}" --headless --config "${root}/настройки.json"
  --model unicode --synthesize "Unicode path check"
  --out "${root}/ձայն.wav" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  TIMEOUT 15)
if(NOT result EQUAL 0 OR NOT EXISTS "${root}/ձայն.wav")
  message(FATAL_ERROR "Unicode CLI synthesis failed (${result}): ${output} ${error}")
endif()
file(REMOVE_RECURSE "${work}")
