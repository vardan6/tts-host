#pragma once

// Private ABI boundary for the pinned llama.cpp b11029 runtime, which ships
// beside the runner (ADR 0010). Loading absolute application-owned libraries
// keeps model packages data-only; their own dependencies, including the CUDA
// libraries ggml-cuda loads, resolve through the executable-directory search.
#include "llama.h"
#include "ggml-backend.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#include "tts_host/path_encoding.hpp"
#include <filesystem>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tts_host {
class QwenLibrary {
 public:
  explicit QwenLibrary(const std::filesystem::path &path) {
#ifdef _WIN32
    handle_ = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle_) throw std::runtime_error("Cannot load pinned Qwen runtime library: " + path_to_utf8(path));
  }
  ~QwenLibrary() {
#ifdef _WIN32
    if (handle_) FreeLibrary(handle_);
#else
    if (handle_) dlclose(handle_);
#endif
  }
  QwenLibrary(const QwenLibrary &) = delete;
  QwenLibrary &operator=(const QwenLibrary &) = delete;
  template<class T> T symbol(const char *name) {
#ifdef _WIN32
    auto value = GetProcAddress(handle_, name);
#else
    auto value = dlsym(handle_, name);
#endif
    if (!value) throw std::runtime_error(std::string("Qwen requires llama.cpp b11029 symbol: ") + name);
    return reinterpret_cast<T>(value);
  }
 private:
#ifdef _WIN32
  HMODULE handle_{};
#else
  void *handle_{};
#endif
};

#define TTS_QWEN_LLAMA_API(X) \
 X(llama_backend_init) X(llama_backend_free) X(llama_model_default_params) \
 X(llama_model_load_from_file) X(llama_model_free) X(llama_context_default_params) \
 X(llama_init_from_model) X(llama_free) X(llama_get_memory) X(llama_memory_clear) \
 X(llama_get_embeddings_ith) X(llama_sampler_chain_default_params) \
 X(llama_sampler_chain_init) X(llama_sampler_chain_add) X(llama_sampler_init_top_k) \
 X(llama_sampler_init_top_p) X(llama_sampler_init_temp) X(llama_sampler_init_dist) \
 X(llama_sampler_sample) X(llama_sampler_free)
#define TTS_QWEN_MTMD_API(X) \
 X(mtmd_context_params_default) X(mtmd_init_from_file) X(mtmd_free) \
 X(mtmd_gen_audio_get_info) X(mtmd_helper_init_opt_default) \
 X(mtmd_helper_bitmap_init_from_buf) X(mtmd_bitmap_free) \
 X(mtmd_helper_gen_audio_init) X(mtmd_helper_gen_audio_free) \
 X(mtmd_helper_gen_audio_set_input) X(mtmd_helper_gen_audio_step_prompt) \
 X(mtmd_helper_gen_audio_step_gen) X(mtmd_helper_gen_audio_get_output)

class QwenApi {
 public:
#ifdef _WIN32
  static constexpr auto base_name = "ggml-base.dll";
  static constexpr auto ggml_name = "ggml.dll";
  static constexpr auto llama_name = "llama.dll";
  static constexpr auto mtmd_name = "mtmd.dll";
#else
  static constexpr auto base_name = "libggml-base.so";
  static constexpr auto ggml_name = "libggml.so";
  static constexpr auto llama_name = "libllama.so";
  static constexpr auto mtmd_name = "libmtmd.so";
#endif
  explicit QwenApi(const std::filesystem::path &directory)
      : base_(directory / base_name), ggml_(directory / ggml_name),
        llama_(directory / llama_name), mtmd_(directory / mtmd_name) {
#define BIND_LLAMA(name) name = llama_.symbol<decltype(name)>(#name);
    TTS_QWEN_LLAMA_API(BIND_LLAMA)
#undef BIND_LLAMA
#define BIND_MTMD(name) name = mtmd_.symbol<decltype(name)>(#name);
    TTS_QWEN_MTMD_API(BIND_MTMD)
#undef BIND_MTMD
    auto load = ggml_.symbol<decltype(&::ggml_backend_load_all_from_path)>("ggml_backend_load_all_from_path");
    const auto path = path_to_utf8(directory);
    load(path.c_str());
    auto count = ggml_.symbol<decltype(&::ggml_backend_dev_count)>("ggml_backend_dev_count");
    auto get = ggml_.symbol<decltype(&::ggml_backend_dev_get)>("ggml_backend_dev_get");
    auto type = base_.symbol<decltype(&::ggml_backend_dev_type)>("ggml_backend_dev_type");
    bool gpu = false;
    for (size_t i = 0; i < count(); ++i) gpu |= type(get(i)) == GGML_BACKEND_DEVICE_TYPE_GPU;
    if (!gpu) throw std::runtime_error("Qwen evaluation requires a working CUDA runtime/GPU; CPU fallback is disabled");
    llama_backend_init();
  }
  ~QwenApi() { llama_backend_free(); }
#define DECLARE(name) decltype(&::name) name{};
  TTS_QWEN_LLAMA_API(DECLARE)
  TTS_QWEN_MTMD_API(DECLARE)
#undef DECLARE
 private:
  QwenLibrary base_, ggml_, llama_, mtmd_;
};
#undef TTS_QWEN_LLAMA_API
#undef TTS_QWEN_MTMD_API
}  // namespace tts_host
